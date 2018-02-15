#!/bin/sh

test_description='colored git blame'
. ./test-lib.sh

PROG='git blame -c'
. "$TEST_DIRECTORY"/annotate-tests.sh

test_expect_success 'colored blame colors contiguous lines' '
	git blame --abbrev=12 --color-lines hello.c >actual.raw &&
	test_decode_color <actual.raw >actual &&
	grep "<BOLD;BLACK>(F" actual > F.expect &&
	grep "<BOLD;BLACK>(H" actual > H.expect &&
	test_line_count = 2 F.expect &&
	test_line_count = 3 H.expect
'

test_expect_success 'colored blame colors continuous fields' '

	git mv hello.c world.c &&
	git commit -a -m "moved file" &&
	cat <<-EOF >> world.c &&
	void world()
	{
		puts("world");
	}
	EOF
	git add world.c &&
	GIT_AUTHOR_NAME="F" GIT_AUTHOR_EMAIL="F@test.git" \
		git commit -m "forgot to add changes to moved file" &&

	git blame --abbrev=12 --color-fields world.c >actual.raw &&
	test_decode_color <actual.raw >actual &&

	grep "<BOLD;BLACK>hello.c" actual > colored_hello.expect &&
	grep "hello.c" actual > all_hello.expect &&
	test_line_count = 9 colored_hello.expect &&
	test_line_count = 10 all_hello.expect &&

	grep "<BOLD;BLACK>world.c" actual > colored_world.expect &&
	grep "world.c" actual > all_world.expect &&
	test_line_count = 3 colored_world.expect &&
	test_line_count = 4 all_world.expect &&

	grep "(F" actual > all_F.expect &&
	grep "<BOLD;BLACK>(F" actual > colored_F.expect &&
	test_line_count = 8 all_F.expect &&
	test_line_count = 5 colored_F.expect &&

	grep "(H" actual > all_H.expect &&
	grep "<BOLD;BLACK>(H" actual > colored_H.expect &&
	test_line_count = 5 all_H.expect &&
	test_line_count = 3 colored_H.expect
'

test_done
