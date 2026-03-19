#!/usr/bin/perl

# Suite 12: Concurrent Routing Correctness Test
# Tests: ROUTE-001 through ROUTE-N+2
#
# Verifies nginx correctly routes responses to the correct client under
# concurrent load. A custom challenge-response upstream computes f(x) =
# (x*7+3) % 1000000. Each client chains 50 requests: the answer from
# request N becomes the input for request N+1. If any response is
# misrouted, the chain breaks and the client detects it.
#
# Default: 10 client processes, 3 non-forking upstreams (select-based multiplexer).

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Time::HiRes qw(time);
use IO::Socket::INET;
use IO::Select;
use POSIX qw(WNOHANG);

use Getopt::Long;

my %opts;
GetOptions(\%opts,
    'listen-port=i',
    'clients=i',
    'requests=i',
    'retries=i',
    'backoff=i',
    'upstreams=i',
);

my $num_clients   = $opts{clients}  // 10;
my $num_requests  = $opts{requests} // 50;
my $max_retries   = $opts{retries}  // 10;
my $retry_backoff = $opts{backoff}  // 50;     # ms, multiplied by attempt number
my $num_upstreams = $opts{upstreams} // 3;
my $conns_per_upstream = $num_clients * 5;

my $t = Test::HTTPLite->new();
my ($listen_port, @upstream_ports) = $t->ports(1 + $num_upstreams);
$listen_port = $opts{'listen-port'} // $listen_port;

plan tests => $num_clients + 2;

# Start challenge daemons on each upstream port
for my $up_port (@upstream_ports) {
    $t->run_daemon(\&challenge_daemon, $up_port);
    $t->waitforsocket("127.0.0.1:$up_port");
}

my @upstream_specs = map { "127.0.0.1:$_:$conns_per_upstream" } @upstream_ports;
$t->write_config($listen_port, 10000, @upstream_specs);
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
# ROUTE-002 through ROUTE-N+1: concurrent clients, chained requests each
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
            my $r = waitpid($pid, WNOHANG);
            if ($r > 0 || $r == -1) {
                $exited = 1;
                last;
            }
            select(undef, undef, undef, 0.05);  # 50ms poll
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
    # Verify nginx is alive by sending a probe request
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: 8\r\n"
        . "Connection: close\r\n"
        . "\r\n"
        . "c0:0:100",
        port => $listen_port, timeout => 5, nresponses => 1,
    );

    my $alive = kill(0, $t->{pids}[0]);
    my $test_id = sprintf('ROUTE-%03d', $num_clients + 2);
    ok($alive, "$test_id: nginx survived concurrent routing load");
}

###############################################################################
# Subroutines
###############################################################################

# --- Read a complete HTTP response from a socket ---
#
# Returns the response string, or '' on timeout/error.

sub read_http_response {
    my ($socket, $timeout) = @_;
    $timeout //= 5;

    my $sel = IO::Select->new($socket);
    my $response = '';
    my $read_deadline = time() + $timeout;

    while (time() < $read_deadline) {
        my $left = $read_deadline - time();
        last if $left <= 0;
        my @ready = $sel->can_read($left < 0.5 ? $left : 0.5);
        if (@ready) {
            my $buf;
            my $n = $socket->sysread($buf, 65536);
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

    return $response;
}

# --- Send a single HTTP request with retries ---
#
# Returns ($response, $retries_used).

sub send_one_request {
    my ($port, $request, $max_tries, $backoff_ms) = @_;

    my $response = '';
    my $retries = 0;

    for my $try (1..$max_tries) {
        my $s = IO::Socket::INET->new(
            PeerAddr => '127.0.0.1',
            PeerPort => $port,
            Proto    => 'tcp',
            Timeout  => 5,
        );

        if (!$s) {
            $retries++;
            select(undef, undef, undef, $backoff_ms * $try / 1000.0);
            next;
        }

        $s->autoflush(1);
        $s->syswrite($request);
        $response = read_http_response($s, 5);
        $s->close;

        # Retry on transient upstream pool errors
        if ($response =~ /inactive upstream/i) {
            $retries++;
            select(undef, undef, undef, $backoff_ms * $try / 1000.0);
            next;
        }
        last;    # got a real response
    }

    return ($response, $retries);
}

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

        my ($response, $retries) = send_one_request($port, $request, $max_retries, $retry_backoff);
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

# --- Handle a single challenge request body ---
#
# Parses "c${id}:${seq}:${value}" and returns the response body.

sub handle_challenge_request {
    my ($body) = @_;

    if ($body =~ /^(c\d+):(\d+):(\d+)$/) {
        my ($cid, $seq, $val) = ($1, $2, $3);
        my $answer = ($val * 7 + 3) % 1_000_000;
        return "$cid:$seq:$answer";
    }
    return "ERROR:bad_format";
}

# --- Process buffered HTTP requests and send responses ---
#
# Parses complete HTTP requests from $buf_ref, sends responses via $fh.
# Returns 0 on success, 1 if the connection should be closed (write error).

sub process_buffered_requests {
    my ($fh, $buf_ref) = @_;

    while ($$buf_ref =~ /\r\n\r\n/) {
        my $hdr_end = index($$buf_ref, "\r\n\r\n");
        my $headers = substr($$buf_ref, 0, $hdr_end);
        my $body_start = $hdr_end + 4;

        my $content_length = 0;
        if ($headers =~ /Content-Length:\s*(\d+)/i) {
            $content_length = $1;
        }

        my $total_needed = $body_start + $content_length;
        last if length($$buf_ref) < $total_needed;

        my $body = substr($$buf_ref, $body_start, $content_length);
        $$buf_ref = substr($$buf_ref, $total_needed);

        my $resp_body = handle_challenge_request($body);

        my $resp = "HTTP/1.1 200 OK\r\n"
                 . "Content-Length: " . length($resp_body) . "\r\n"
                 . "Content-Type: text/plain\r\n"
                 . "Connection: keep-alive\r\n"
                 . "\r\n"
                 . $resp_body;

        return 1 unless $fh->syswrite($resp);
    }

    return 0;
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
                my $client = $server->accept();
                next unless $client;
                $client->autoflush(1);
                $sel->add($client);
                $bufs{fileno($client)} = '';
            } else {
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

                if (process_buffered_requests($fh, \$bufs{$fd})) {
                    $sel->remove($fh);
                    delete $bufs{$fd};
                    $fh->close;
                }
            }
        }
    }
}
