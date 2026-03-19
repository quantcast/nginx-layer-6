package Test::HTTPLite;

# Test helper module for HTTPLite nginx module tests.
# Modeled after nginx's own Test::Nginx (hg.nginx.org/nginx-tests).

use warnings;
use strict;

use Cwd qw(abs_path);
use File::Temp ();
use File::Path qw(rmtree);
use IO::Socket::INET;
use IO::Select;
use LWP::UserAgent;
use HTTP::Request;
use POSIX qw(:sys_wait_h SIGQUIT SIGKILL SIGTERM);
use Test::More;
use Time::HiRes qw(sleep time usleep);

my @_daemons;   # child pids to clean up

sub new {
    my ($class, %opts) = @_;

    my $self = bless {
        binary   => ($ENV{TEST_HTTPLITE_BINARY}
                     ? abs_path($ENV{TEST_HTTPLITE_BINARY})
                     : abs_path(__FILE__ . '/../../../../nginx/out/sbin/nginx')),
        tempdir  => File::Temp::tempdir('/tmp/httplite_test_XXXXXX', CLEANUP => 0),
        pids     => [],
        daemons  => [],
        port     => undef,
    }, $class;

    return $self;
}

sub has_plan {
    my ($self, $n) = @_;
    Test::More::plan(tests => $n);
    return $self;
}

# --- Config generation ---------------------------------------------------

sub write_config {
    my ($self, $listen_port, $keep_alive, @upstreams) = @_;
    $keep_alive //= 10000;

    my $upstream_block = '';
    for my $u (@upstreams) {
        my ($host, $port, $conns) = split /:/, $u;
        $conns //= 5;
        $upstream_block .= "        server ${host}:${port} connections=${conns};\n";
    }

    my $conf = <<"CONF";
daemon off;
master_process off;
error_log $self->{tempdir}/error.log debug;

events {
    worker_connections 512;
}

httplite {
    server {
        listen ${listen_port};
        server_name 127.0.0.1;
    }

    upstreams {
        keep_alive ${keep_alive};
${upstream_block}    }
}
CONF

    my $path = "$self->{tempdir}/nginx.conf";
    open my $fh, '>', $path or die "Cannot write config: $!";
    print $fh $conf;
    close $fh;

    $self->{_conf} = $path;
    $self->{port}  = $listen_port;
    return $self;
}

sub write_config_raw {
    my ($self, $content) = @_;

    # Substitute __TEMPDIR__ for the temp directory
    $content =~ s/__TEMPDIR__/$self->{tempdir}/g;

    my $path = "$self->{tempdir}/nginx.conf";
    open my $fh, '>', $path or die "Cannot write config: $!";
    print $fh $content;
    close $fh;

    $self->{_conf} = $path;
    return $self;
}

# --- LWP helpers ----------------------------------------------------------

sub ua {
    my ($self, %opts) = @_;
    my $timeout    = $opts{timeout}    || 5;
    my $keep_alive = $opts{keep_alive} // 0;  # Default to no keep-alive

    my $ua = LWP::UserAgent->new(
        timeout    => $timeout,
        keep_alive => $keep_alive,
    );
    return $ua;
}

sub base_url {
    my ($self, $port) = @_;
    $port //= $self->{port};
    return "http://127.0.0.1:$port";
}

# --- Process lifecycle ----------------------------------------------------

sub run {
    my ($self, $port) = @_;
    $port //= $self->{port};

    my $conf = $self->{_conf} or die "No config written; call write_config first";

    mkdir "$self->{tempdir}/logs";

    my $pid = fork();
    die "fork failed: $!" unless defined $pid;

    if ($pid == 0) {
        # child
        open STDOUT, '>', "$self->{tempdir}/stdout.log";
        open STDERR, '>', "$self->{tempdir}/stderr.log";
        exec($self->{binary}, '-c', $conf, '-p', $self->{tempdir})
            or exit(127);
    }

    push @{$self->{pids}}, $pid;

    if ($port) {
        $self->waitforsocket("127.0.0.1:$port")
            or warn "nginx did not start listening on port $port within timeout";
    } else {
        sleep 0.5;
    }

    return $self;
}

sub run_expect_fail {
    my ($self) = @_;

    my $conf = $self->{_conf} or die "No config written";

    mkdir "$self->{tempdir}/logs";

    my $pid = fork();
    die "fork failed: $!" unless defined $pid;

    if ($pid == 0) {
        open STDOUT, '>', "$self->{tempdir}/stdout.log";
        open STDERR, '>', "$self->{tempdir}/stderr.log";
        exec($self->{binary}, '-c', $conf, '-p', $self->{tempdir})
            or exit(127);
    }

    # Wait up to 2s for the process to exit
    my $exited = 0;
    for (1..40) {
        my $r = waitpid($pid, WNOHANG);
        if ($r > 0 || $r == -1) {
            $exited = 1;
            last;
        }
        sleep 0.05;
    }

    if (!$exited) {
        # Still running -- unexpected success, track for cleanup
        push @{$self->{pids}}, $pid;
    }

    return $exited;
}

sub run_daemon {
    my ($self, $sub, @args) = @_;

    my $pid = fork();
    die "fork failed: $!" unless defined $pid;

    if ($pid == 0) {
        $sub->(@args);
        exit(0);
    }

    push @{$self->{daemons}}, $pid;
    push @_daemons, $pid;
    return $pid;
}

sub stop {
    my ($self) = @_;

    for my $pid (@{$self->{pids}}) {
        kill SIGQUIT, $pid;
    }

    # Give processes up to 3s to exit
    my $deadline = time() + 3;
    for my $pid (@{$self->{pids}}) {
        while (time() < $deadline) {
            last if waitpid($pid, WNOHANG) != 0;
            sleep 0.05;
        }
        kill SIGKILL, $pid;
        waitpid($pid, 0);
    }
    $self->{pids} = [];

    for my $pid (@{$self->{daemons}}) {
        kill SIGTERM, $pid;
        sleep 0.05;
        kill SIGKILL, $pid;
        waitpid($pid, WNOHANG);
    }
    $self->{daemons} = [];

    return $self;
}

# --- HTTP helpers ---------------------------------------------------------

sub http {
    my ($self, $request, %opts) = @_;

    my $host       = $opts{host}       || '127.0.0.1';
    my $port       = $opts{port}       || $self->{port};
    my $timeout    = $opts{timeout}    || 5;
    my $nresponses = $opts{nresponses} || 0;

    my $s = IO::Socket::INET->new(
        PeerAddr => $host,
        PeerPort => $port,
        Proto    => 'tcp',
        Timeout  => $timeout,
    );
    return undef unless $s;

    $s->autoflush(1);
    $s->syswrite($request);

    if ($opts{start}) {
        # Return socket for split send/receive
        return $s;
    }

    return _http_read($s, $timeout, $nresponses);
}

sub http_get {
    my ($self, $path, %opts) = @_;
    $path //= '/';
    my $port = $opts{port} || $self->{port};

    my $request = "GET $path HTTP/1.1\r\n"
                . "Host: 127.0.0.1:$port\r\n"
                . "User-Agent: httplite-test/1.0\r\n"
                . "Accept: */*\r\n"
                . "Connection: keep-alive\r\n"
                . "\r\n";

    $opts{nresponses} //= 1;
    return $self->http($request, %opts);
}

sub http_start {
    my ($self, $request, %opts) = @_;
    return $self->http($request, %opts, start => 1);
}

sub http_end {
    my ($self, $socket, %opts) = @_;
    return undef unless defined $socket;
    my $timeout = $opts{timeout} || 5;
    my $nresponses = $opts{nresponses} || 0;
    return _http_read($socket, $timeout, $nresponses);
}

sub _http_read {
    my ($s, $timeout, $nresponses) = @_;
    $nresponses //= 0;  # 0 = read until EOF/timeout

    my $response = '';
    my $sel = IO::Select->new($s);
    my $responses_seen = 0;

    my $deadline = time() + $timeout;
    while (time() < $deadline) {
        my $remaining = $deadline - time();
        last if $remaining <= 0;
        my @ready = $sel->can_read($remaining < 0.5 ? $remaining : 0.5);
        if (@ready) {
            my $buf;
            my $n = $s->sysread($buf, 65536);
            if (!defined $n || $n == 0) {
                last;  # EOF or error
            }
            $response .= $buf;

            # Count complete HTTP responses if we have a target
            if ($nresponses > 0) {
                $responses_seen = _count_complete_responses($response);
                last if $responses_seen >= $nresponses;
            }
        }
    }

    $s->close;
    return $response;
}

# Count the number of complete HTTP responses in a buffer.
# A complete response has headers ending with \r\n\r\n and a body
# of Content-Length bytes (or 0 if no Content-Length).
sub _count_complete_responses {
    my ($data) = @_;
    my $count = 0;
    my $pos = 0;

    while ($pos < length($data)) {
        my $hdr_end = index($data, "\r\n\r\n", $pos);
        last if $hdr_end < 0;

        my $headers = substr($data, $pos, $hdr_end - $pos);
        my $body_start = $hdr_end + 4;

        my $content_length = 0;
        if ($headers =~ /Content-Length:\s*(\d+)/i) {
            $content_length = $1;
        }

        my $total = $body_start + $content_length;
        last if $total > length($data);

        $count++;
        $pos = $total;
    }
    return $count;
}

# --- Port allocation ------------------------------------------------------

sub ports {
    my ($self, $count) = @_;
    $count //= 1;
    my @ports;
    for (1..$count) {
        my $s = IO::Socket::INET->new(
            LocalAddr => '127.0.0.1',
            LocalPort => 0,
            Proto     => 'tcp',
            Listen    => 1,
            ReuseAddr => 1,
        );
        die "Cannot allocate port: $!" unless $s;
        push @ports, $s->sockport();
        $s->close;
    }
    return @ports;
}

# --- Socket helpers -------------------------------------------------------

sub waitforsocket {
    my ($self, $peer, $timeout) = @_;
    $timeout //= 5;

    my ($host, $port) = split /:/, $peer;
    my $deadline = time() + $timeout;

    while (time() < $deadline) {
        my $s = IO::Socket::INET->new(
            PeerAddr => $host,
            PeerPort => $port,
            Proto    => 'tcp',
            Timeout  => 0.5,
        );
        if ($s) {
            $s->close;
            return 1;
        }
        sleep 0.1;
    }
    return 0;
}

sub waitforportclose {
    my ($self, $port, $timeout) = @_;
    $timeout //= 5;

    my $deadline = time() + $timeout;
    while (time() < $deadline) {
        my $s = IO::Socket::INET->new(
            PeerAddr => '127.0.0.1',
            PeerPort => $port,
            Proto    => 'tcp',
            Timeout  => 0.5,
        );
        if (!$s) {
            return 1;
        }
        $s->close;
        sleep 0.1;
    }
    return 0;
}

# --- File helpers ---------------------------------------------------------

sub read_file {
    my ($self, $name) = @_;
    my $path = "$self->{tempdir}/$name";
    open my $fh, '<', $path or return undef;
    local $/;
    return <$fh>;
}

sub testdir {
    my ($self) = @_;
    return $self->{tempdir};
}

# --- Built-in echo daemon ------------------------------------------------

sub echo_daemon {
    my ($port) = @_;

    my $server = IO::Socket::INET->new(
        LocalAddr => '127.0.0.1',
        LocalPort => $port,
        Proto     => 'tcp',
        Listen    => 128,
        ReuseAddr => 1,
        ReusePort => 1,
    ) or die "echo_daemon: cannot bind port $port: $!";

    while (my $client = $server->accept()) {
        # Handle each connection in a child process for concurrency
        my $pid = fork();
        next if $pid;  # parent continues accepting

        if (!defined $pid) {
            # fork failed, handle inline
            _echo_handle($client);
            exit(0);
        }

        # child
        $server->close;
        $SIG{PIPE} = 'IGNORE';
        _echo_handle($client);
        exit(0);
    }
}

sub _echo_handle {
    my ($client) = @_;

    $client->autoflush(1);

    my $sel = IO::Select->new($client);
    my $buf = '';

    # Read and respond to HTTP requests
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

            # Do we have the full body?
            my $total_needed = $body_start + $content_length;
            last if length($buf) < $total_needed;

            my $body = substr($buf, $body_start, $content_length);
            $buf = substr($buf, $total_needed);

            # Send HTTP response echoing the body
            my $resp_body = $body;
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

# --- Cleanup --------------------------------------------------------------

sub DESTROY {
    my ($self) = @_;
    local $?;  # preserve exit status during cleanup
    $self->stop();
}

END {
    local $?;  # preserve exit status during cleanup
    for my $pid (@_daemons) {
        kill SIGTERM, $pid;
        kill SIGKILL, $pid;
        waitpid($pid, WNOHANG);
    }
}

1;
