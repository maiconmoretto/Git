#!/bin/sh
#
# Copyright (c) 2017 Johannes E. Schindelin
#

test_description='git rebase -i --recreate-merges

This test runs git rebase "interactively", retaining the branch structure by
recreating merge commits.

Initial setup:

    -- B --                   (first)
   /       \
 A - C - D - E - H            (master)
       \       /
	 F - G                (second)
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'setup' '
	write_script replace-editor.sh <<-\EOF &&
	mv "$1" "$(git rev-parse --git-path ORIGINAL-TODO)"
	cp script-from-scratch "$1"
	EOF

	test_commit A &&
	git checkout -b first &&
	test_commit B &&
	git checkout master &&
	test_commit C &&
	test_commit D &&
	git merge --no-commit B &&
	test_tick &&
	git commit -m E &&
	git tag -m E E &&
	git checkout -b second C &&
	test_commit F &&
	test_commit G &&
	git checkout master &&
	git merge --no-commit G &&
	test_tick &&
	git commit -m H &&
	git tag -m H H
'

cat >script-from-scratch <<\EOF
label onto

# onebranch
pick G
pick D
label onebranch

# second
reset onto
pick B
label second

reset onto
merge -C H second
merge onebranch # Merge the topic branch 'onebranch'
EOF

test_cmp_graph () {
	cat >expect &&
	git log --graph --boundary --format=%s "$@" >output &&
	sed "s/ *$//" <output >output.trimmed &&
	test_cmp expect output.trimmed
}

test_expect_success 'create completely different structure' '
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	git rebase -i --recreate-merges A &&
	test_cmp_graph <<-\EOF
	*   Merge the topic branch '\''onebranch'\''
	|\
	| * D
	| * G
	* |   H
	|\ \
	| |/
	|/|
	| * B
	|/
	* A
	EOF
'

test_expect_success 'generate correct todo list' '
	cat >expect <<-\EOF &&
	label onto

	reset onto
	pick d9df450 B
	label E

	reset onto
	pick 5dee784 C
	label branch-point
	pick ca2c861 F
	pick 088b00a G
	label H

	reset branch-point # C
	pick 12bd07b D
	merge -C 2051b56 E # E
	merge -C 233d48a H # H

	EOF

	grep -v "^#" <.git/ORIGINAL-TODO >output &&
	test_cmp expect output
'

test_expect_success 'with a branch tip that was cherry-picked already' '
	git checkout -b already-upstream master &&
	base="$(git rev-parse --verify HEAD)" &&

	test_commit A1 &&
	test_commit A2 &&
	git reset --hard $base &&
	test_commit B1 &&
	test_tick &&
	git merge -m "Merge branch A" A2 &&

	git checkout -b upstream-with-a2 $base &&
	test_tick &&
	git cherry-pick A2 &&

	git checkout already-upstream &&
	test_tick &&
	git rebase -i --recreate-merges upstream-with-a2 &&
	test_cmp_graph upstream-with-a2.. <<-\EOF
	*   Merge branch A
	|\
	| * A1
	* | B1
	|/
	o A2
	EOF
'

test_expect_success 'do not rebase cousins unless asked for' '
	write_script copy-editor.sh <<-\EOF &&
	cp "$1" "$(git rev-parse --git-path ORIGINAL-TODO)"
	EOF

	test_config sequence.editor \""$PWD"/copy-editor.sh\" &&
	git checkout -b cousins master &&
	before="$(git rev-parse --verify HEAD)" &&
	test_tick &&
	git rebase -i --recreate-merges HEAD^ &&
	test_cmp_rev HEAD $before &&
	test_tick &&
	git rebase -i --recreate-merges=rebase-cousins HEAD^ &&
	test_cmp_graph HEAD^.. <<-\EOF
	*   Merge the topic branch '\''onebranch'\''
	|\
	| * D
	| * G
	|/
	o H
	EOF
'

test_expect_success 'refs/rewritten/* is worktree-local' '
	git worktree add wt &&
	cat >wt/script-from-scratch <<-\EOF &&
	label xyz
	exec GIT_DIR=../.git git rev-parse --verify refs/rewritten/xyz >a || :
	exec git rev-parse --verify refs/rewritten/xyz >b
	EOF

	test_config -C wt sequence.editor \""$PWD"/replace-editor.sh\" &&
	git -C wt rebase -i HEAD &&
	test_must_be_empty wt/a &&
	test_cmp_rev HEAD "$(cat wt/b)"
'

test_expect_success 'post-rewrite hook and fixups work for merges' '
	git checkout -b post-rewrite &&
	test_commit same1 &&
	git reset --hard HEAD^ &&
	test_commit same2 &&
	git merge -m "to fix up" same1 &&
	echo same old same old >same2.t &&
	test_tick &&
	git commit --fixup HEAD same2.t &&
	fixup="$(git rev-parse HEAD)" &&

	mkdir -p .git/hooks &&
	test_when_finished "rm .git/hooks/post-rewrite" &&
	echo "cat >actual" | write_script .git/hooks/post-rewrite &&

	test_tick &&
	git rebase -i --autosquash --recreate-merges HEAD^^^ &&
	printf "%s %s\n%s %s\n%s %s\n%s %s\n" >expect $(git rev-parse \
		$fixup^^2 HEAD^2 \
		$fixup^^ HEAD^ \
		$fixup^ HEAD \
		$fixup HEAD) &&
	test_cmp expect actual
'

test_done
