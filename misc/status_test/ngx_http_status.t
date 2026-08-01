#!/usr/bin/perl

# (C) Blitzy Agent

# Runtime tests for the HTTP status code registry over HTTP/1.x.
#
# What the unit driver beside this file cannot reach is executed here: the two
# places an upstream's status crosses into nginx's own structures, the gate that
# crossing hands it to, the gate an error_page overwrite passes, and the status
# method of the embedded Perl module.  Every one of those is a function that a
# request reaches through a cycle, a pool and a filter chain, so it is reached
# by driving a real nginx rather than by calling into it.
#
# The wire output and the access log are asserted against both builds, because a
# build configured with --with-http_status_validation must send exactly what a
# build without it sends: the switch adds reporting and refusing and changes
# nothing a client can observe about a status nginx was always willing to send.
# What the switch does add is asserted where the two builds differ, each
# assertion naming the build it holds for.
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

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy rewrite perl/)->plan(28);

# A build configured with the switch reports, and a report is an alert, so the
# alerts this file provokes are expected rather than a failure of it.  They are
# asserted one by one below.

# A build configured with the switch reports, and a report is an alert, so the
# alerts this file provokes are expected rather than a failure of it.  They are
# asserted one by one below, and counted at the end.

$t->todo_alerts();

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    log_format  status  '$request_uri $status';

    access_log  %%TESTDIR%%/status.log  status;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        add_header  X-Status  $status  always;

        # the direct crossing: the status is relayed exactly as it arrived

        location /relay/unknown {
            proxy_pass http://127.0.0.1:8081;
        }

        location /relay/low {
            proxy_pass http://127.0.0.1:8082;
        }

        # the other crossing: an error of the upstream's is intercepted and
        # answered by nginx's own error page machinery

        location /intercept/kept {
            proxy_pass http://127.0.0.1:8081;
            proxy_intercept_errors on;
            error_page 599 /page;
        }

        location /intercept/same {
            proxy_pass http://127.0.0.1:8081;
            proxy_intercept_errors on;
            error_page 599 =599 /page;
        }

        location /intercept/described {
            proxy_pass http://127.0.0.1:8081;
            proxy_intercept_errors on;
            error_page 599 =200 /page;
        }

        # the error page of an intercepted status raising a status of its own,
        # which reaches the same gate a second time for the same request

        location /intercept/again {
            proxy_pass http://127.0.0.1:8081;
            proxy_intercept_errors on;
            error_page 599 /raise;
        }

        location /raise {
            return 598;
        }

        # a status nginx itself chose, returned by a handler

        location /handler {
            error_page 599 /page;
            return 599;
        }

        location /page {
            return 200 "PAGE\n";
        }

        # the status method of the embedded Perl module, which is the one place
        # a status arrives as a signed integer of a script's choosing

        location /perl/negative {
            perl 'sub {
                my $r = shift;
                $r->status(-1);
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

        location /perl/extreme {
            perl 'sub {
                my $r = shift;
                $r->status(2147483647);
                $r->send_http_header("text/plain");
                return 0;
            }';
        }

        location /perl/teapot {
            perl 'sub {
                my $r = shift;
                $r->status(418);
                $r->send_http_header("text/plain");
                return 0 if $r->header_only;
                $r->print("teapot\n");
                return 0;
            }';
        }

        location /perl/highest {
            perl 'sub {
                my $r = shift;
                $r->status(999);
                $r->send_http_header("text/plain");
                return 0 if $r->header_only;
                $r->print("highest\n");
                return 0;
            }';
        }
    }
}

EOF

$t->run_daemon(\&unknown_daemon);
$t->run_daemon(\&low_daemon);
$t->run()->waitforsocket('127.0.0.1:' . port(8081));
$t->waitforsocket('127.0.0.1:' . port(8082));

###############################################################################

my $strict = $t->has_module('http_status_validation');

# The two crossings, whose whole contract is that the status is relayed as it
# stands.  A status the registry does not describe and a status below the range
# it describes at all are both accepted by the status line parser, so both are
# relayed, and the status line the upstream sent is relayed with them.

like(http_get('/relay/unknown'), qr!^HTTP/1.1 599 Nonesuch!,
	'a status the registry does not describe is relayed');
like(http_get('/relay/unknown'), qr/FROM-599/, 'with the body of the upstream');

like(http_get('/relay/low'), qr!^HTTP/1.1 042 Odd!,
	'a status below the range the registry describes is relayed');
like(http_get('/relay/low'), qr/FROM-042/, 'with the body of the upstream');

# And the interception, where such a status enters nginx's own error page
# machinery.  The status is the upstream's until an overwrite replaces it, and
# the body is nginx's from the moment the error is intercepted.

like(http_get('/intercept/kept'), qr!^HTTP/1.1 599 \r.*PAGE!s,
	'an intercepted status is kept and answered by nginx');
like(http_get('/intercept/same'), qr!^HTTP/1.1 599 \r.*PAGE!s,
	'an overwrite naming the same number is answered the same way');
like(http_get('/intercept/described'), qr!^HTTP/1.1 200 OK\r.*PAGE!s,
	'an overwrite naming another status replaces it');

# And the error page of an intercepted status raising a status of its own, which
# is nginx choosing one for a request that is being answered from an upstream.
# The exemption is the origin of the response and not the value of the
# status, so it is the request's for as long as the request has an upstream: a
# status chosen for it after that is exempt with the status the upstream chose,
# which is wider than the exemption need be and costs nothing, every status
# nginx itself chooses being one the registry describes.  What is asserted here
# is that the response is answered; that it is not reported is asserted with
# the reports below.

like(http_get('/intercept/again'), qr!^HTTP/1.1 598 !,
	'a status raised while answering an intercepted one is answered');

# A status nginx chose for itself takes the same machinery, reaching it as the
# value a handler returned rather than through either crossing.

like(http_get('/handler'), qr!^HTTP/1.1 599 \r.*PAGE!s,
	'a status a handler returned is answered by nginx');

# The status a response is recorded as, which the access log and the $status
# variable are the two readers of.  Both are formatted in three digits, so a
# status below 100 is recorded with a leading zero.

like(http_get('/relay/unknown'), qr/X-Status: 599/,
	'the variable holds a relayed status');
like(http_get('/relay/low'), qr/X-Status: 042/,
	'the variable holds a relayed status below 100');

# The embedded Perl setter, the one status in nginx that is neither written by
# the source that chose it nor bounded as it is parsed.  A negative value, a
# value of four digits and a value at the top of a signed integer are refused in
# every build, because what they would reach is a field of exactly three digits.

like(http_get('/perl/negative'), qr!^HTTP/1.1 500 !,
	'a negative status from a script is refused');
like(http_get('/perl/wide'), qr!^HTTP/1.1 500 !,
	'a status of four digits from a script is refused');
like(http_get('/perl/extreme'), qr!^HTTP/1.1 500 !,
	'a status at the top of a signed integer is refused');

# While a status of three digits that the registry merely does not describe is
# sent by the build without the switch and refused by the build with it, which
# is the whole of the difference between them.

if ($strict) {
	like(http_get('/perl/teapot'), qr!^HTTP/1.1 500 !,
		'an undescribed status from a script is refused (strict)');
	like(http_get('/perl/highest'), qr!^HTTP/1.1 500 !,
		'the highest such status is refused (strict)');

} else {
	like(http_get('/perl/teapot'), qr!^HTTP/1.1 418 .*teapot!s,
		'an undescribed status from a script is sent');
	like(http_get('/perl/highest'), qr!^HTTP/1.1 999 .*highest!s,
		'the highest such status is sent');
}

my $log = $t->read_file('error.log');

# Every refusal of the setter reaches the script as a croak, which nginx logs
# as the handler having failed, so a refusal is never silent.

is(scalar(() = $log =~ /status\(\): invalid status code/g), $strict ? 5 : 3,
	'each status a script had refused was reported to the script');

# What the switch adds.  A status an upstream chose is exempt wherever it is
# examined, at the direct crossing and at the gate an interception hands it to
# alike, so neither is reported however far the code is from anything the
# registry describes.

is(alerts($log, 'unregistered HTTP status 599', '/relay/unknown'), 0,
	'a relayed status is not reported');
is(alerts($log, 'unregistered HTTP status 42', '/relay/low'), 0,
	'a relayed status below 100 is not reported');
is(alerts($log, 'unregistered HTTP status 599', '/intercept/kept'), 0,
	'an intercepted status is not reported');

# And a status the configuration chose for that same response is reported, even
# where the directive names the number it replaces: authorship is read from what
# the crossing recorded and never from the status having some value.

is(alerts($log, 'unregistered HTTP status 599', '/intercept/same'),
	$strict ? 1 : 0,
	'an overwrite naming the number it replaces is reported');
is(alerts($log, 'unregistered HTTP status 599', '/handler'), $strict ? 1 : 0,
	'a status a handler returned is reported');
# While a status nginx chose for a request that has an upstream is exempt with
# the status that upstream chose, the exemption being the request's origin and
# not the value of either.

is(alerts($log, 'unregistered HTTP status 598', '/intercept/again'), 0,
	'a status raised while answering an intercepted one is not reported');
is(alerts($log, 'unregistered HTTP status 418', '/perl/teapot'),
	$strict ? 1 : 0,
	'a status a script chose is reported');

# A status refused for being too wide is refused before the registry is asked
# about it, so it is never reported as undescribed as well.

is(alerts($log, 'unregistered HTTP status', '/perl/wide')
	+ alerts($log, 'unregistered HTTP status', '/perl/extreme')
	+ alerts($log, 'unregistered HTTP status', '/perl/negative'), 0,
	'a status refused for its width is not reported as undescribed');

like($t->read_file('status.log'), qr!^/relay/unknown 599$!m,
	'the access log holds the status of the response');
like($t->read_file('status.log'), qr!^/relay/low 042$!m,
	'the access log holds a status below 100 with a leading zero');

# And nothing else was reported at all, which is what closes the assertions
# above: each of them names what must be reported for one request, and this
# names the number of alerts the whole run may make.  The build with the switch
# makes six: one apiece for the four statuses reported above, from whichever of
# the three points chose each of them, and one more for each of the two a
# script chose, which the status method logs itself when the setter refuses it
# and before it croaks.  The build without the switch makes none.

is(scalar(() = $log =~ /^.+\[alert\].+$/gm), $strict ? 6 : 0,
	'nothing besides those was reported');

###############################################################################

# The alerts naming one message and one request URI.  An alert carries the
# message this file asserts and then whatever context nginx had for it, the
# request among that context, so the two are looked for in the one line.

sub alerts {
	my ($log, $msg, $uri) = @_;
	my $n = 0;

	for my $line ($log =~ /^.+\[alert\].+$/gm) {
		next if index($line, $msg) < 0;
		next if $line !~ /request: "\w+ \Q$uri\E[ ?]/;
		$n++;
	}

	return $n;
}

###############################################################################

# The two upstreams, each answering with a status line of its own writing: an
# upstream is the one author of a status that nginx neither chose nor bounded,
# so it is one written here rather than one nginx was asked to produce.

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
