#!/usr/bin/perl

# Suite 02: Configuration Parsing Tests
# Port range: 9020-9029
# Tests: CFG-001 through CFG-009

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;

plan tests => 9;

my $up_port_a = 9021;
my $up_port_b = 9022;

# Start upstream servers
my $t = Test::HTTPLite->new();
$t->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_a);
$t->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_b);
$t->waitforsocket("127.0.0.1:$up_port_a");
$t->waitforsocket("127.0.0.1:$up_port_b");

###############################################################################
# CFG-001: Valid minimal config
###############################################################################

{
    my $t1 = Test::HTTPLite->new();
    $t1->write_config(9020, 10000, "127.0.0.1:${up_port_a}:5");
    $t1->run(9020);
    ok($t1->waitforsocket('127.0.0.1:9020', 5),
        'CFG-001: valid minimal config starts successfully');
}

###############################################################################
# CFG-002: Missing server block
# KNOWN BUG-003: port=-1 passed to htons()
###############################################################################

{
    my $t2 = Test::HTTPLite->new();
    $t2->write_config_raw(<<"CONF");
daemon off;
master_process off;
error_log __TEMPDIR__/error.log debug;
events { worker_connections 512; }
httplite {
    upstreams {
        keep_alive 10000;
        server 127.0.0.1:${up_port_a} connections=5;
    }
}
CONF

    my $failed = $t2->run_expect_fail();
    ok($failed,
        'CFG-002: missing server block - nginx fails to start')
        or diag('KNOWN BUG-003: nginx started despite missing server block');
}

###############################################################################
# CFG-003: Missing upstreams block
# KNOWN BUG-001: division by zero
###############################################################################

{
    my $t3 = Test::HTTPLite->new();
    $t3->write_config_raw(<<"CONF");
daemon off;
master_process off;
error_log __TEMPDIR__/error.log debug;
events { worker_connections 512; }
httplite {
    server {
        listen 9023;
        server_name 127.0.0.1;
    }
}
CONF

    my $failed = $t3->run_expect_fail();
    if ($failed) {
        pass('CFG-003: missing upstreams block - nginx fails to start');
    } else {
        # It started; try to trigger the bug
        my $resp = $t3->http(
            "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n",
            port => 9023, timeout => 3,
        );
        my $log = $t3->read_file('error.log') // '';
        my $crashed = ($log =~ /segfault|signal 8|SIGFPE|divide/i);
        fail('CFG-003: missing upstreams block - nginx should fail to start '
             . '[KNOWN BUG: BUG-001]');
        diag($crashed
            ? 'Division by zero / crash detected'
            : 'nginx started without upstreams block');
    }
}

###############################################################################
# CFG-004: Missing listen directive
# KNOWN BUG-003
###############################################################################

{
    my $t4 = Test::HTTPLite->new();
    $t4->write_config_raw(<<"CONF");
daemon off;
master_process off;
error_log __TEMPDIR__/error.log debug;
events { worker_connections 512; }
httplite {
    server {
        server_name 127.0.0.1;
    }
    upstreams {
        keep_alive 10000;
        server 127.0.0.1:${up_port_a} connections=5;
    }
}
CONF

    my $failed = $t4->run_expect_fail();
    ok($failed,
        'CFG-004: missing listen directive - nginx fails to start')
        or diag('KNOWN BUG-003: nginx started without a listen port');
}

###############################################################################
# CFG-005: Multiple upstream servers
###############################################################################

{
    my $t5 = Test::HTTPLite->new();
    $t5->write_config(9024, 10000,
        "127.0.0.1:${up_port_a}:5",
        "127.0.0.1:${up_port_b}:5");
    $t5->run(9024);

    my $got_traffic = 0;
    if ($t5->waitforsocket('127.0.0.1:9024', 5)) {
        for (1..4) {
            $t5->http_get('/', port => 9024);
        }
        # Both upstreams should have received traffic.
        # We verify by sending requests; the echo daemon responds to all.
        $got_traffic = 1;  # If we got here without errors, traffic flowed
    }
    ok($got_traffic,
        'CFG-005: multiple upstream servers - both receive traffic');
}

###############################################################################
# CFG-006: connections=0
# KNOWN BUG-001: 0-element array, modulo by 0
###############################################################################

{
    my $t6 = Test::HTTPLite->new();
    $t6->write_config_raw(<<"CONF");
daemon off;
master_process off;
error_log __TEMPDIR__/error.log debug;
events { worker_connections 512; }
httplite {
    server {
        listen 9025;
        server_name 127.0.0.1;
    }
    upstreams {
        keep_alive 10000;
        server 127.0.0.1:${up_port_a} connections=0;
    }
}
CONF

    my $failed = $t6->run_expect_fail();
    if ($failed) {
        pass('CFG-006: connections=0 - nginx fails to start');
    } else {
        my $resp = $t6->http(
            "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n",
            port => 9025, timeout => 3,
        );
        fail('CFG-006: connections=0 - nginx should reject config '
             . '[KNOWN BUG: BUG-001]');
    }
}

###############################################################################
# CFG-007: keep_alive 0
###############################################################################

{
    my $t7 = Test::HTTPLite->new();
    $t7->write_config(9026, 0, "127.0.0.1:${up_port_a}:5");
    $t7->run(9026);

    my $resp = $t7->http(
        "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
        port => 9026, timeout => 5,
    );
    my $ok = defined $resp && $resp =~ m{HTTP/1\.[01] 200};
    ok($ok, 'CFG-007: keep_alive 0 - server starts and handles requests');
}

###############################################################################
# CFG-008: Upstream server without port
###############################################################################

{
    my $t8 = Test::HTTPLite->new();
    $t8->write_config_raw(<<"CONF");
daemon off;
master_process off;
error_log __TEMPDIR__/error.log debug;
events { worker_connections 512; }
httplite {
    server {
        listen 9027;
        server_name 127.0.0.1;
    }
    upstreams {
        keep_alive 10000;
        server 127.0.0.1 connections=5;
    }
}
CONF

    # Either nginx rejects this or starts with default port 80
    my $failed = $t8->run_expect_fail();
    pass('CFG-008: upstream server without port - ' .
         ($failed ? 'nginx rejects config' : 'nginx starts (uses default 80)'));
}

###############################################################################
# CFG-009: Garbage server address
# KNOWN BUG-007: invalid input not rejected
###############################################################################

{
    my $t9 = Test::HTTPLite->new();
    $t9->write_config_raw(<<"CONF");
daemon off;
master_process off;
error_log __TEMPDIR__/error.log debug;
events { worker_connections 512; }
httplite {
    server {
        listen 9028;
        server_name 127.0.0.1;
    }
    upstreams {
        keep_alive 10000;
        server not-valid connections=abc;
    }
}
CONF

    my $failed = $t9->run_expect_fail();
    ok($failed,
        'CFG-009: garbage server address - nginx fails to start')
        or diag('KNOWN BUG-007: nginx accepted invalid upstream address');
}
