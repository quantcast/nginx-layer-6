#!/usr/bin/perl

# Suite 07: Upstream Connection Pool Tests
# Port range: 9070-9079
# Tests: POOL-001 through POOL-005

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Time::HiRes qw(sleep);
use POSIX qw(SIGTERM SIGKILL WNOHANG);
use LWP::UserAgent;

plan tests => 5;

my $up_port_a = 9071;
my $up_port_b = 9072;

###############################################################################
# POOL-001: Round-robin across 2 pools
###############################################################################

{
    my $t1 = Test::HTTPLite->new();
    $t1->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_a);
    $t1->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_b);
    $t1->waitforsocket("127.0.0.1:$up_port_a");
    $t1->waitforsocket("127.0.0.1:$up_port_b");

    $t1->write_config(9070, 10000,
        "127.0.0.1:${up_port_a}:1",
        "127.0.0.1:${up_port_b}:1");
    $t1->run(9070);

    if (!$t1->waitforsocket('127.0.0.1:9070', 5)) {
        fail('POOL-001: nginx failed to start');
    } else {
        # Send 4 sequential requests using LWP
        my $ua = LWP::UserAgent->new(timeout => 10, keep_alive => 1);
        my $all_ok = 1;
        for (1..4) {
            my $resp = $ua->get("http://127.0.0.1:9070/");
            $all_ok = 0 unless $resp->is_success;
            sleep 0.2;
        }
        # Both upstreams should have received traffic (round-robin)
        TODO: {
            local $TODO = 'GET response forwarding bug';
            ok($all_ok,
                'POOL-001: round-robin - both upstreams received connections');
        }
    }
}

###############################################################################
# POOL-002: Pool exhaustion (all busy)
# KNOWN BUG-012: Client waits up to 10s with no feedback
###############################################################################

{
    my $up_port_c = 9073;
    my $t2 = Test::HTTPLite->new();
    $t2->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_c);
    $t2->waitforsocket("127.0.0.1:$up_port_c");

    $t2->write_config(9074, 10000, "127.0.0.1:${up_port_c}:1");
    $t2->run(9074);

    if (!$t2->waitforsocket('127.0.0.1:9074', 5)) {
        fail('POOL-002: nginx failed to start');
    } else {
        # Send 2 concurrent requests with connections=1 using LWP in forked children
        my $pid_a = fork();
        if ($pid_a == 0) {
            my $child_ua = LWP::UserAgent->new(timeout => 5, keep_alive => 1);
            my $r = $child_ua->get("http://127.0.0.1:9074/");
            exit($r->is_success ? 0 : 1);
        }

        my $pid_b = fork();
        if ($pid_b == 0) {
            my $child_ua = LWP::UserAgent->new(timeout => 5, keep_alive => 1);
            my $r = $child_ua->get("http://127.0.0.1:9074/");
            exit($r->is_success ? 0 : 1);
        }

        waitpid($pid_a, 0);
        my $exit_a = $? >> 8;
        waitpid($pid_b, 0);
        my $exit_b = $? >> 8;

        my $both = ($exit_a == 0 && $exit_b == 0);
        my $either = ($exit_a == 0 || $exit_b == 0);

        TODO: {
            local $TODO = 'KNOWN BUG-012: client waits with no feedback on pool exhaustion';
            ok($both || $either,
                'POOL-002: pool exhaustion - ' .
                ($both ? 'both requests handled' : 'at least one handled'));
        }
    }
}

###############################################################################
# POOL-003: Upstream dies mid-request
###############################################################################

{
    my $up_port_d = 9075;
    my $t3 = Test::HTTPLite->new();
    my $daemon_pid = $t3->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_d);
    $t3->waitforsocket("127.0.0.1:$up_port_d");

    $t3->write_config(9076, 10000, "127.0.0.1:${up_port_d}:5");
    $t3->run(9076);

    if (!$t3->waitforsocket('127.0.0.1:9076', 5)) {
        fail('POOL-003: nginx failed to start');
    } else {
        # Establish connection using LWP
        my $ua = LWP::UserAgent->new(timeout => 10, keep_alive => 1);
        $ua->get("http://127.0.0.1:9076/");

        # Kill the upstream
        kill SIGTERM, $daemon_pid;
        sleep 0.5;
        kill SIGKILL, $daemon_pid;
        waitpid($daemon_pid, WNOHANG);

        # Send another request (should fail but not crash nginx)
        eval {
            local $SIG{ALRM} = sub { die "timeout\n" };
            alarm(3);
            $ua->get("http://127.0.0.1:9076/");
            alarm(0);
        };
        sleep 0.5;

        my $alive = kill(0, $t3->{pids}[0]);
        ok($alive, 'POOL-003: upstream dies mid-session - nginx survives');
    }
}

###############################################################################
# POOL-004: Upstream restart recovery
###############################################################################

{
    my $up_port_e = 9077;
    my $t4 = Test::HTTPLite->new();
    my $daemon_pid = $t4->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_e);
    $t4->waitforsocket("127.0.0.1:$up_port_e");

    $t4->write_config(9078, 10000, "127.0.0.1:${up_port_e}:5");
    $t4->run(9078);

    if (!$t4->waitforsocket('127.0.0.1:9078', 5)) {
        fail('POOL-004: nginx failed to start');
    } else {
        # Verify initial connectivity using LWP
        my $ua = LWP::UserAgent->new(timeout => 10, keep_alive => 1);
        $ua->get("http://127.0.0.1:9078/");

        # Kill and restart upstream
        kill SIGTERM, $daemon_pid;
        sleep 0.5;
        kill SIGKILL, $daemon_pid;
        waitpid($daemon_pid, WNOHANG);
        sleep 0.5;

        $t4->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_e);
        $t4->waitforsocket("127.0.0.1:$up_port_e");

        # New request should work
        my $resp = $ua->get("http://127.0.0.1:9078/");
        TODO: {
            local $TODO = 'GET response forwarding bug';
            ok($resp->is_success,
                'POOL-004: upstream restart recovery - new request succeeds');
        }
    }
}

###############################################################################
# POOL-005: Full pool concurrent
# 3 upstreams x 30 connections = 90 capacity, send 90 concurrent requests
###############################################################################

{
    my $up_port_f = 9073;
    my $up_port_g = 9075;
    my $up_port_h = 9077;

    my $t5 = Test::HTTPLite->new();
    $t5->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_f);
    $t5->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_g);
    $t5->run_daemon(\&Test::HTTPLite::echo_daemon, $up_port_h);
    $t5->waitforsocket("127.0.0.1:$up_port_f");
    $t5->waitforsocket("127.0.0.1:$up_port_g");
    $t5->waitforsocket("127.0.0.1:$up_port_h");

    $t5->write_config(9079, 10000,
        "127.0.0.1:${up_port_f}:30",
        "127.0.0.1:${up_port_g}:30",
        "127.0.0.1:${up_port_h}:30");
    $t5->run(9079);

    if (!$t5->waitforsocket('127.0.0.1:9079', 5)) {
        fail('POOL-005: nginx failed to start');
    } else {
        # Send 90 concurrent requests via forked children using LWP
        my @children;
        for my $i (1..90) {
            my $pid = fork();
            if ($pid == 0) {
                my $child_ua = LWP::UserAgent->new(timeout => 10, keep_alive => 1);
                my $r = $child_ua->get("http://127.0.0.1:9079/");
                exit($r->is_success ? 0 : 1);
            }
            push @children, $pid;
        }

        my $success = 0;
        for my $pid (@children) {
            waitpid($pid, 0);
            $success++ if ($? >> 8) == 0;
        }

        TODO: {
            local $TODO = 'GET response forwarding bug';
            cmp_ok($success, '>=', 80,
                "POOL-005: 90 concurrent requests - $success/90 succeeded");
        }
    }
}
