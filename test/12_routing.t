#!/usr/bin/perl

# Suite 12: Concurrent Routing Correctness Test
# Port range: 9120-9129
# Tests: ROUTE-001 through ROUTE-007
#
# Verifies nginx correctly routes responses to the correct client under
# concurrent load. A custom challenge-response upstream computes f(x) =
# (x*7+3) % 1000000. Each client chains 50 requests: the answer from
# request N becomes the input for request N+1. If any response is
# misrouted, the chain breaks and the client detects it.

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Time::HiRes qw(sleep usleep time);
use IO::Socket::INET;
use IO::Select;

plan tests => 7;

my $listen_port   = 9120;
my $upstream_port = 9121;
my $num_clients   = 5;
my $num_requests  = 50;

my $t = Test::HTTPLite->new();
$t->run_daemon(\&challenge_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config($listen_port, 10000, "127.0.0.1:${upstream_port}:10");
$t->run($listen_port);

die "nginx failed to start on port $listen_port"
    unless $t->waitforsocket("127.0.0.1:$listen_port", 5);

###############################################################################
# ROUTE-001: Smoke test - single challenge-response roundtrip
###############################################################################

{
    my $body = "c0:0:100";
    my $len  = length $body;
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: $len\r\n"
        . "Connection: close\r\n"
        . "\r\n"
        . $body,
        timeout => 5, nresponses => 1,
    );

    # Expected: 100 * 7 + 3 = 703
    my $ok = defined $resp
        && $resp =~ /HTTP\/1\.[01] 200/
        && $resp =~ /c0:0:703/;
    ok($ok, 'ROUTE-001: smoke test - single challenge-response roundtrip')
        or diag('Response: ' . (defined $resp ? substr($resp, 0, 200) : '<undef>'));
}

###############################################################################
# ROUTE-002 through ROUTE-006: 5 concurrent clients, 50 chained requests each
###############################################################################

my $tempdir = $t->testdir();
my @children;

for my $cid (1..$num_clients) {
    my $pid = fork();
    die "fork failed: $!" unless defined $pid;

    if ($pid == 0) {
        # Prevent child's DESTROY from killing shared nginx/daemon processes
        $t->{pids} = [];
        $t->{daemons} = [];
        @Test::HTTPLite::_daemons = ();
        eval { run_client($cid, $num_requests, $listen_port, $tempdir); };
        if ($@) {
            if (open my $fh, '>', "$tempdir/client_${cid}.result") {
                print $fh "0\nCRASH: $@\n";
                close $fh;
            }
            exit(1);
        }
        # run_client calls exit(), never returns
    }

    push @children, $pid;
}

# Wait for all children with 60s overall deadline
my $deadline = time() + 60;

for my $pid (@children) {
    my $remaining = $deadline - time();
    if ($remaining > 0) {
        my $exited = 0;
        while (time() < $deadline) {
            my $r = waitpid($pid, 1);    # WNOHANG
            if ($r > 0 || $r == -1) {
                $exited = 1;
                last;
            }
            usleep(50_000);              # 50ms poll
        }
        if (!$exited) {
            kill 9, $pid;
            waitpid($pid, 0);
        }
    } else {
        kill 9, $pid;
        waitpid($pid, 0);
    }
}

# Read results and assert per-client
for my $cid (1..$num_clients) {
    my $result_file = "$tempdir/client_${cid}.result";
    my $count  = 0;
    my $detail = '';

    if (open my $fh, '<', $result_file) {
        $count = <$fh>;
        chomp $count if defined $count;
        $count = int($count || 0);
        local $/;
        $detail = <$fh> // '';
        close $fh;
    }

    my $test_id = sprintf('ROUTE-%03d', $cid + 1);
    is($count, $num_requests,
        "$test_id: client $cid completed $count/$num_requests chained requests")
        or diag("Client $cid failures:\n$detail");
}

###############################################################################
# ROUTE-007: nginx survived concurrent load
###############################################################################

{
    sleep 0.5;
    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'ROUTE-007: nginx survived concurrent routing load');
}

###############################################################################
# Subroutines
###############################################################################

# --- Client process (forked child) ---

sub run_client {
    my ($client_id, $total, $port, $dir) = @_;

    my $seed = int(rand(900)) + 100;   # 100-999
    my $current = $seed;
    my $success = 0;
    my @failures;

    for my $seq (1..$total) {
        my $body = "c${client_id}:${seq}:${current}";
        my $len  = length $body;

        my $request = "POST / HTTP/1.1\r\n"
                    . "Host: 127.0.0.1\r\n"
                    . "Content-Length: $len\r\n"
                    . "Connection: close\r\n"
                    . "\r\n"
                    . $body;

        my $s = IO::Socket::INET->new(
            PeerAddr => '127.0.0.1',
            PeerPort => $port,
            Proto    => 'tcp',
            Timeout  => 5,
        );

        if (!$s) {
            push @failures, "seq=$seq: connect failed: $!";
            last;
        }

        $s->autoflush(1);
        $s->syswrite($request);

        # Read response with 5s deadline
        my $sel = IO::Select->new($s);
        my $response = '';
        my $read_deadline = time() + 5;
        while (time() < $read_deadline) {
            my $left = $read_deadline - time();
            last if $left <= 0;
            my @ready = $sel->can_read($left < 0.5 ? $left : 0.5);
            if (@ready) {
                my $buf;
                my $n = $s->sysread($buf, 65536);
                last if !defined $n || $n == 0;
                $response .= $buf;
            }
        }
        $s->close;

        # Verify response
        my $expected = ($current * 7 + 3) % 1_000_000;
        my $expected_body = "c${client_id}:${seq}:${expected}";

        if ($response =~ /HTTP\/1\.[01] 200/ && index($response, $expected_body) >= 0) {
            $success++;
            $current = $expected;    # chain: feed answer into next request
        } else {
            my $actual_body = '';
            if ($response =~ /\r\n\r\n(.*)$/s) {
                $actual_body = $1;
            }
            push @failures, "seq=$seq: expected='$expected_body' got='$actual_body'";
            last;                    # chain is broken
        }
    }

    # Write results
    if (open my $fh, '>', "$dir/client_${client_id}.result") {
        print $fh "$success\n";
        print $fh "$_\n" for @failures;
        close $fh;
    }

    exit($success == $total ? 0 : 1);
}

# --- Challenge-response upstream daemon ---

sub challenge_daemon {
    my ($port) = @_;

    my $server = IO::Socket::INET->new(
        LocalAddr => '127.0.0.1',
        LocalPort => $port,
        Proto     => 'tcp',
        Listen    => 128,
        ReuseAddr => 1,
        ReusePort => 1,
    ) or die "challenge_daemon: cannot bind port $port: $!";

    while (my $client = $server->accept()) {
        my $pid = fork();
        next if $pid;    # parent continues accepting

        if (!defined $pid) {
            _challenge_handle($client);
            exit(0);
        }

        # child
        $server->close;
        $SIG{PIPE} = 'IGNORE';
        _challenge_handle($client);
        exit(0);
    }
}

sub _challenge_handle {
    my ($client) = @_;

    $client->autoflush(1);

    my $sel = IO::Select->new($client);
    my $buf = '';

    while (1) {
        my @ready = $sel->can_read(2);
        last unless @ready;
        my $data;
        my $n = $client->sysread($data, 65536);
        last if !defined $n || $n == 0;
        $buf .= $data;

        # Process all complete HTTP requests in buffer
        while ($buf =~ /\r\n\r\n/) {
            my $hdr_end = index($buf, "\r\n\r\n");
            my $headers = substr($buf, 0, $hdr_end);
            my $body_start = $hdr_end + 4;

            my $content_length = 0;
            if ($headers =~ /Content-Length:\s*(\d+)/i) {
                $content_length = $1;
            }

            my $total_needed = $body_start + $content_length;
            last if length($buf) < $total_needed;

            my $body = substr($buf, $body_start, $content_length);
            $buf = substr($buf, $total_needed);

            # Challenge-response: parse "c${id}:${seq}:${value}"
            my $resp_body;
            if ($body =~ /^(c\d+):(\d+):(\d+)$/) {
                my ($cid, $seq, $val) = ($1, $2, $3);
                my $answer = ($val * 7 + 3) % 1_000_000;
                $resp_body = "$cid:$seq:$answer";
            } else {
                $resp_body = "ERROR:bad_format";
            }

            # Random delay 0-5ms to increase response interleaving
            usleep(int(rand(5000)));

            my $resp = "HTTP/1.1 200 OK\r\n"
                     . "Content-Length: " . length($resp_body) . "\r\n"
                     . "Content-Type: text/plain\r\n"
                     . "Connection: keep-alive\r\n"
                     . "\r\n"
                     . $resp_body;

            $client->syswrite($resp) or last;
        }
    }

    $client->close;
}
