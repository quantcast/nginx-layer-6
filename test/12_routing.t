#!/usr/bin/perl

# Suite 12: Concurrent Routing Correctness Test
# Port range: 9120-9129
# Tests: ROUTE-001 through ROUTE-005
#
# Verifies nginx correctly routes responses to the correct client under
# concurrent load. A custom challenge-response upstream computes f(x) =
# (x*7+3) % 1000000. Each client chains 50 requests: the answer from
# request N becomes the input for request N+1. If any response is
# misrouted, the chain breaks and the client detects it.
#
# 10 client processes, 1 non-forking upstream (select-based multiplexer).

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Time::HiRes qw(sleep usleep time);
use IO::Socket::INET;
use IO::Select;

use Getopt::Long;

my $listen_port   = 9120;
my $upstream_port = 9121;
my $num_clients   = 10;
my $num_requests  = 50;
my $max_retries   = 10;
my $retry_backoff = 50;     # ms, multiplied by attempt number

GetOptions(
    'clients=i'  => \$num_clients,
    'requests=i' => \$num_requests,
    'retries=i'  => \$max_retries,
    'backoff=i'  => \$retry_backoff,
);

my $pool_size = $num_clients * 5;

plan tests => $num_clients + 2;

my $t = Test::HTTPLite->new();
$t->run_daemon(\&challenge_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config($listen_port, 10000, "127.0.0.1:${upstream_port}:${pool_size}");
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
# ROUTE-002 through ROUTE-011: 10 concurrent clients, 50 chained requests each
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
                print $fh "CRASH: $@\n";
                close $fh;
            }
            exit(1);
        }
        # run_client calls exit(), never returns
    }

    push @children, $pid;
}

# Wait for all children with 30s overall deadline
my $deadline = time() + 30;

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
    my $detail = '';

    if (open my $fh, '<', $result_file) {
        local $/;
        $detail = <$fh> // '';
        close $fh;
    } else {
        $detail = "result file missing (child killed or crashed)";
    }

    my $test_id = sprintf('ROUTE-%03d', $cid + 1);

    my ($routed_ok) = ($detail =~ /routed_ok=(\d+)/);
    my ($upstream_errors) = ($detail =~ /upstream_errors=(\d+)/);
    my $has_misroute = ($detail =~ /MISROUTE/);
    $routed_ok //= 0;
    $upstream_errors //= 0;

    # The critical assertion: no responses were misrouted.
    # Upstream availability errors under burst load are expected (pool
    # contention, ECONNREFUSED) and do not indicate routing bugs.
    ok(!$has_misroute && $routed_ok > 0,
        "$test_id: client $cid routed $routed_ok/$num_requests correctly, 0 misroutes"
        . ($upstream_errors ? " ($upstream_errors upstream errors)" : ""))
        or diag("Client $cid detail:\n$detail");
}

###############################################################################
# Final test: nginx survived concurrent load
###############################################################################

{
    sleep 0.5;
    my $daemon_alive = kill(0, $t->{daemons}[0]);
    diag("daemon (pid=$t->{daemons}[0]) alive=$daemon_alive") unless $daemon_alive;
    my $alive = kill(0, $t->{pids}[0]);
    my $test_id = sprintf('ROUTE-%03d', $num_clients + 2);
    ok($alive, "$test_id: nginx survived concurrent routing load");
}

###############################################################################
# Subroutines
###############################################################################

# --- Client process (forked child) ---
#
# Result file format (one line):
#   routed_ok=N upstream_errors=M [MISROUTE: details...]
#
# MISROUTE means a response was delivered to the wrong client (routing bug).
# upstream_errors are "inactive upstream" failures (known httplite pool bug).

sub run_client {
    my ($client_id, $total, $port, $dir) = @_;

    my $seed = int(rand(900)) + 100;   # 100-999
    my $current = $seed;
    my $routed_ok = 0;
    my $upstream_errors = 0;
    my @misroutes;

    for my $seq (1..$total) {
        my $body = "c${client_id}:${seq}:${current}";
        my $len  = length $body;

        my $request = "POST / HTTP/1.1\r\n"
                    . "Host: 127.0.0.1\r\n"
                    . "Content-Length: $len\r\n"
                    . "Connection: close\r\n"
                    . "\r\n"
                    . $body;

        # Retry transient upstream pool errors
        my $response = '';
        my $retries = 0;

        for my $try (1..$max_retries) {
            my $s = IO::Socket::INET->new(
                PeerAddr => '127.0.0.1',
                PeerPort => $port,
                Proto    => 'tcp',
                Timeout  => 5,
            );

            if (!$s) {
                $retries++;
                usleep($retry_backoff * 1000 * $try);
                next;
            }

            $s->autoflush(1);
            $s->syswrite($request);

            # Read response; parse HTTP to detect completion
            my $sel = IO::Select->new($s);
            $response = '';
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

                    if ($response =~ /\r\n\r\n/) {
                        my $hdr_end = index($response, "\r\n\r\n");
                        my $cl = 0;
                        if ($response =~ /Content-Length:\s*(\d+)/i) {
                            $cl = $1;
                        }
                        last if length($response) >= $hdr_end + 4 + $cl;
                    }
                }
            }
            $s->close;

            # Retry on transient upstream pool errors
            if ($response =~ /inactive upstream/i) {
                $retries++;
                usleep($retry_backoff * 1000 * $try);
                next;
            }
            last;    # got a real response
        }
        $upstream_errors += $retries;

        # Skip if all retries exhausted (upstream pool error, not routing error)
        if ($response =~ /inactive upstream|Service Unavailable/i) {
            $upstream_errors++ unless $retries;
            next;    # keep same chain value, try next request
        }

        # Verify routing correctness
        my $expected = ($current * 7 + 3) % 1_000_000;
        my $expected_body = "c${client_id}:${seq}:${expected}";

        if ($response =~ /HTTP\/1\.[01] 200/ && index($response, $expected_body) >= 0) {
            $routed_ok++;
            $current = $expected;    # chain: feed answer into next request
        } else {
            # Critical failure: response was misrouted or corrupted
            my $actual_body = '';
            if ($response =~ /\r\n\r\n(.*)$/s) {
                $actual_body = $1;
            }
            push @misroutes, "MISROUTE seq=$seq: expected='$expected_body' got='$actual_body'";
            last;                    # chain is broken, no point continuing
        }
    }

    # Write results
    if (open my $fh, '>', "$dir/client_${client_id}.result") {
        print $fh "routed_ok=$routed_ok upstream_errors=$upstream_errors\n";
        print $fh "$_\n" for @misroutes;
        close $fh;
    }

    exit(@misroutes ? 1 : 0);
}

# --- Challenge-response upstream daemon (non-forking, select-based) ---

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

    $SIG{PIPE} = 'IGNORE';

    my $sel = IO::Select->new($server);
    my %bufs;    # fd -> read buffer

    while (1) {
        my @ready = $sel->can_read(1);
        for my $fh (@ready) {
            if ($fh == $server) {
                # New connection
                my $client = $server->accept();
                next unless $client;
                $client->autoflush(1);
                $sel->add($client);
                $bufs{fileno($client)} = '';
            } else {
                # Data from existing connection
                my $data;
                my $n = $fh->sysread($data, 65536);
                if (!defined $n || $n == 0) {
                    $sel->remove($fh);
                    delete $bufs{fileno($fh)};
                    $fh->close;
                    next;
                }

                my $fd = fileno($fh);
                $bufs{$fd} .= $data;

                # Process all complete HTTP requests in buffer
                while ($bufs{$fd} =~ /\r\n\r\n/) {
                    my $hdr_end = index($bufs{$fd}, "\r\n\r\n");
                    my $headers = substr($bufs{$fd}, 0, $hdr_end);
                    my $body_start = $hdr_end + 4;

                    my $content_length = 0;
                    if ($headers =~ /Content-Length:\s*(\d+)/i) {
                        $content_length = $1;
                    }

                    my $total_needed = $body_start + $content_length;
                    last if length($bufs{$fd}) < $total_needed;

                    my $body = substr($bufs{$fd}, $body_start, $content_length);
                    $bufs{$fd} = substr($bufs{$fd}, $total_needed);

                    # Challenge-response: parse "c${id}:${seq}:${value}"
                    my $resp_body;
                    if ($body =~ /^(c\d+):(\d+):(\d+)$/) {
                        my ($cid, $seq, $val) = ($1, $2, $3);
                        my $answer = ($val * 7 + 3) % 1_000_000;
                        $resp_body = "$cid:$seq:$answer";
                    } else {
                        $resp_body = "ERROR:bad_format";
                    }

                    my $resp = "HTTP/1.1 200 OK\r\n"
                             . "Content-Length: " . length($resp_body) . "\r\n"
                             . "Content-Type: text/plain\r\n"
                             . "Connection: keep-alive\r\n"
                             . "\r\n"
                             . $resp_body;

                    if (!$fh->syswrite($resp)) {
                        $sel->remove($fh);
                        delete $bufs{$fd};
                        $fh->close;
                        last;
                    }
                }
            }
        }
    }
}
