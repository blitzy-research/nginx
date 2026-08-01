#!/usr/bin/perl

# (C) Blitzy Agent

# Runtime tests for the HTTP status code registry over HTTP/2 and HTTP/3.
#
# Those two encodings carry the status of a response in a ":status" field of
# exactly three digits, written by a conversion whose width is a minimum and
# never a limit, so what may be written there is what the whole width bound
# exists for.  This file drives the real encoders with every status that reaches
# them from a source nginx does not choose the value of: a status an upstream
# sent that the registry does not describe, one below the range it describes at
# all, one a handler returned, and the three a script may name.
#
# The bound belongs to those two encodings and to nothing else, and it is held
# where each of them reserves that field.  It is not held where a status is
# chosen: an HTTP/1.x status line reserves NGX_INT_T_LEN bytes for the same
# number and carries any width, and a configuration has always been able to ask
# for one --
#
#     error_page 404 =1234 /wide;
#
# has always been accepted and has always answered "HTTP/1.1 1234 " -- so a
# bound applied where a status is chosen would refuse that as well, in the build
# without the switch too, which is the one build whose behaviour must not differ
# from the behaviour before the registry existed.  That the directive is still
# accepted is asserted here, and that its response is still sent is asserted
# over HTTP/1.x in ngx_http_status.t beside this file.
#
# So a wide status does reach these encoders, from a configuration that asks for
# one, and each of them refuses the response rather than writing past the field
# it reserved.  Both halves are asserted: that the one source which can name a
# wide status without a configuration, a script, is stopped where it names it,
# and that a wide status a configuration did ask for is refused by each encoder
# while everything narrower is encoded faithfully.
#
# It needs the external nginx test harness, which is not part of this
# repository.  TEST_NGINX_LIB names its lib directory:
#
#     make -f misc/status_test/GNUmakefile runtime \
#         TEST_NGINX_LIB=/path/to/nginx-tests/lib

###############################################################################

use warnings;
use strict;

use Test::More;

use IO::Socket::INET;

BEGIN {
	my $lib = $ENV{TEST_NGINX_LIB};

	if (!defined $lib || !-r "$lib/Test/Nginx.pm") {
		print "1..0 # SKIP TEST_NGINX_LIB does not name the lib " .
			"directory of the external nginx test harness\n";
		exit 0;
	}

	unshift @INC, $lib;
}

use Test::Nginx;
use Test::Nginx::HTTP2;
use Test::Nginx::HTTP3;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()
	->has(qw/http http_v2 http_v3 proxy rewrite perl cryptx/)
	->has_daemon('openssl')->plan(17);

# Reporting is an alert, and so is an encoder refusing a response it could not
# write.  Every alert this file provokes is asked for by it and counted at the
# end, so the harness's own check that a run made none of them is stood down for
# every build: a build without the switch reports nothing but still refuses the
# two responses of four digits, and a build with the switch reports as well.

$t->todo_alerts();

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    http2 on;

    ssl_certificate_key localhost.key;
    ssl_certificate localhost.crt;

    server {
        listen       127.0.0.1:%%PORT_8980_UDP%% quic;
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /relay/unknown {
            proxy_pass http://127.0.0.1:8081;
        }

        location /relay/low {
            proxy_pass http://127.0.0.1:8082;
        }

        location /handler {
            return 599;
        }

        location /perl/teapot {
            perl 'sub {
                my $r = shift;
                $r->status(418);
                $r->send_http_header("text/plain");
                return 0;
            }';
        }

        location /perl/highest {
            perl 'sub {
                my $r = shift;
                $r->status(999);
                $r->send_http_header("text/plain");
                return 0;
            }';
        }

        location /perl/wide {
            perl 'sub {
                my $r = shift;
                $r->status(1000);
                $r->send_http_header("text/plain");
                return 0;
            }';
        }

        # the one source that can put a status of four digits into a response:
        # an overwrite of an error_page, which is accepted where it is written
        # because an HTTP/1.x response carries it

        location /wide/overwrite {
            error_page 598 =1000 /page;
            return 598;
        }

        location /page {
            return 200 "PAGE\n";
        }
    }
}

EOF

$t->write_file('openssl.conf', <<EOF);
[ req ]
default_bits = 2048
encrypt_key = no
distinguished_name = req_distinguished_name
[ req_distinguished_name ]
EOF

my $d = $t->testdir();

foreach my $name ('localhost') {
	system('openssl req -x509 -new '
		. "-config $d/openssl.conf -subj /CN=$name/ "
		. "-out $d/$name.crt -keyout $d/$name.key "
		. ">>$d/openssl.out 2>&1") == 0
		or die "Can't create certificate for $name: $!\n";
}

# The configuration that must be refused, written before nginx is started so
# that it is tested with the same binary and never left running.

my ($perl5lib) = $t->test_globals() =~ /(env\s+PERL5LIB=\S+;)/;
$perl5lib = '' unless defined $perl5lib;

foreach my $overwrite ('1000', '599') {
	$t->write_file("wide$overwrite.conf", <<"EOF");
$perl5lib
daemon off;
pid wide$overwrite.pid;
error_log wide$overwrite.log;
events {
}
http {
    access_log off;

    server {
        listen       127.0.0.1:8083;
        server_name  localhost;
        error_page   599 =$overwrite /page;
        location / {
            return 599;
        }
        location /page {
            return 200 "PAGE\\n";
        }
    }
}
EOF
}

$t->run_daemon(\&unknown_daemon);
$t->run_daemon(\&low_daemon);
$t->run()->waitforsocket('127.0.0.1:' . port(8081));
$t->waitforsocket('127.0.0.1:' . port(8082));

###############################################################################

my $strict = $t->has_module('http_status_validation');

# What each of the two encodings must carry for each of those statuses.  The
# undescribed status of three digits is the one the two builds answer
# differently, a build with the switch refusing it where the setter is what is
# handed it, so the expected field is chosen by the build.

my @want = (
	[ '/relay/unknown', '599', 'a relayed status' ],
	[ '/relay/low', '042', 'a relayed status below 100' ],
	[ '/handler', '599', 'a status a handler returned' ],
	[ '/perl/teapot', $strict ? '500' : '418', 'a status a script chose' ],
	[ '/perl/highest', $strict ? '500' : '999', 'the highest such status' ],
	[ '/perl/wide', '500', 'a status of four digits from a script' ],
);

foreach my $case (@want) {
	my ($uri, $status, $name) = @$case;

	my $s = Test::Nginx::HTTP2->new();
	my $sid = $s->new_stream({ path => $uri });
	my $frames = $s->read(all => [{ sid => $sid, fin => 1 }]);

	my ($frame) = grep { $_->{type} eq "HEADERS" } @$frames;

	is($frame->{headers}->{':status'}, $status, "$name over HTTP/2");
}

foreach my $case (@want) {
	my ($uri, $status, $name) = @$case;

	my $s = Test::Nginx::HTTP3->new();
	my $sid = $s->new_stream({ path => $uri });
	my $frames = $s->read(all => [{ sid => $sid, fin => 1 }]);

	my ($frame) = grep { $_->{type} eq "HEADERS" } @$frames;

	is($frame->{headers}->{':status'}, $status, "$name over HTTP/3");
}

# The one status a configuration supplies directly.  It is not bounded where the
# directive is parsed, in either build: a status of four digits is what an
# HTTP/1.x response has always been able to carry, so naming one is accepted
# exactly as it was before the registry existed, and a status of three digits
# the registry does not describe is accepted with it.

like(conf_test('wide1000.conf'), qr/test is successful/,
	'an overwrite of four digits is accepted where it is written');
like(conf_test('wide599.conf'), qr/test is successful/,
	'an overwrite of three digits the registry does not describe is too');

# And so a wide status does reach these two encoders, which is where the bound
# is.  Each of them refuses the response rather than writing four digits into
# the three bytes it reserved, so the stream carries no header block at all: it
# is reset, which is what a response that could not be written looks like to a
# client.  Whether it was reset is what is asked, and not how, an encoder
# failing after the response has begun leaving nginx no way to say more.

foreach my $proto ('HTTP/2', 'HTTP/3') {
	my $s = $proto eq 'HTTP/2'
		? Test::Nginx::HTTP2->new()
		: Test::Nginx::HTTP3->new();

	my $sid = $s->new_stream({ path => '/wide/overwrite' });
	my $frames = $s->read(all => [{ sid => $sid, fin => 1 }]);

	my ($frame) = grep { $_->{type} eq "HEADERS" } @$frames;

	ok(!defined $frame,
		"a status of four digits is refused over $proto");
}

# And nothing besides the reports each build makes.  Each status above was
# requested once over each of the two encodings, so a build with the switch
# reports twice for the status a handler returned and four times for each status
# a script chose, once from the gate and once from the setter, while a build
# without the switch reports nothing.  Each build then adds the two alerts of
# the two encoders refusing a wide response, and the build with the switch one
# report apiece for the status the overwrite named, which the gate answering
# the error_page reads before either encoder is reached.

is(scalar(() = $t->read_file('error.log') =~ /^.+\[alert\].+$/gm),
	$strict ? 14 : 2, 'nothing besides those was reported');

###############################################################################

# A configuration tested with the binary under test, which answers whether it
# would have been accepted and never runs it.

sub conf_test {
	my ($conf) = @_;

	return `$Test::Nginx::NGINX -t -p $d/ -c $conf -e $conf.log 2>&1`;
}

###############################################################################

sub daemon {
	my ($port, $line, $body) = @_;

	my $server = IO::Socket::INET->new(
		Proto => 'tcp',
		LocalHost => '127.0.0.1:' . port($port),
		Listen => 5,
		Reuse => 1
	)
		or die "Can't create listening socket: $!\n";

	local $SIG{PIPE} = 'IGNORE';

	while (my $client = $server->accept()) {
		$client->autoflush(1);

		while (<$client>) {
			last if (/^\x0d?\x0a?$/);
		}

		print $client "HTTP/1.1 $line\x0d\x0a";
		print $client "Content-Length: " . length($body) . "\x0d\x0a";
		print $client "Connection: close\x0d\x0a\x0d\x0a";
		print $client $body;

		close $client;
	}
}

sub unknown_daemon {
	daemon(8081, '599 Nonesuch', "FROM-599\n");
}

sub low_daemon {
	daemon(8082, '042 Odd', "FROM-042\n");
}

###############################################################################
