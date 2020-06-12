#!/bin/bash
#
# STEF - a Simple TEst Framework.
#
# (c) Jan Pechanec <jp@devnull.cz>
#

typeset -i fail=0
typeset -i untested=0
typeset -i ret
typeset diffout=stef-diff.out
typeset testnames
typeset testfiles
typeset output=stef-output-file.data
typeset tests
typeset LS=/bin/ls
typeset STEF_CONFIG=stef-config

typeset -i STEF_UNSUPPORTED=100
typeset -i STEF_UNTESTED=101
# So that test scripts may use those.
export STEF_UNSUPPORTED
export STEF_UNTESTED

function catoutput
{
	[[ -s $output ]] || return
	echo "--- 8< BEGIN output ---"
	cat $output
	echo "--- 8< END output ---"
}

# If the first argument is a directory, go in there.
if (( $# > 0 )); then
	if [[ -d $1 ]]; then
		cd $1
		shift
	fi
fi

# If you want to use variables defined in here in the tests, export them in
# there.
[[ -f $STEF_CONFIG ]] && source ./$STEF_CONFIG

[[ -z $STEF_TESTSUITE_NAME ]] && STEF_TESTSUITE_NAME="STEF Test Run"
printf "=== [ $STEF_TESTSUITE_NAME ] ===\n"

# If this variable is set, the test suite clone specific settings may put in
# that file.  For example, stuff that each user will need to set differently,
# like mysh, myed, or mytar binaries.
if [[ -n $STEF_CONFIG_LOCAL && -f $STEF_CONFIG_LOCAL ]]; then
	echo "Sourcing test suite specific configuration: $STEF_CONFIG_LOCAL"
	source ./$STEF_CONFIG_LOCAL
fi

echo "Checking configuration sanity."
if [[ -n $STEF_UNCONFIGURE ]]; then
	if [[ -n $STEF_UNCONFIGURE_NEVER && -n $STEF_UNCONFIGURE_ALWAYS ]]; then
		printf "\n"
		printf "STEF_UNCONFIGURE_(ALWAYS|NEVER) are mutually exclusive."
		echo "\nPlease fix it before trying to re-run.  Exiting."
		exit 1
	fi
fi

if [[ -n $STEF_EXECUTABLE_LOCAL_VARS ]]; then
	echo "Checking existence of executables provided by the" \
	    "following variables: $STEF_EXECUTABLE_LOCAL_VARS"
	for var in $STEF_EXECUTABLE_LOCAL_VARS; do
		varval=$(eval echo \$$var)
		[[ -x $varval && $varval == /* ]] && continue

		if [[ $varval != /* ]]; then
			printf "\n%s%s\n%s\n" \
			    "Variable '$var' set as '$varval' as part of " \
			    "STEF_EXECUTABLE_LOCAL_VARS" \
			    "defined in $STEF_CONFIG must be an absolute path."
			printf "\nPlease fix it before trying to re-run.  "
			printf "Exiting.\n"
			exit 1
		fi

		printf "\n%s%s\n%s\n" \
		    "Variable '$var' set as '$varval' as part of " \
		    "STEF_EXECUTABLE_LOCAL_VARS" \
		    "defined in '$STEF_CONFIG' does not point to an executable."
		printf "\nPlease fix it before trying to re-run.  Exiting.\n"
		exit 1
	done
fi

if [[ -n $STEF_REGFILE_LOCAL_VARS ]]; then
	echo "Checking existence of regular files provided by the" \
	    "following variables: $STEF_REGFILE_LOCAL_VARS"
	for var in $STEF_REGFILE_LOCAL_VARS; do
		varval=$(eval echo \$$var)
		[[ -f $varval && $varval == /* ]] && continue

		if [[ $varval != /* ]]; then
			printf "\n%s%s\n%s\n" \
			    "Variable '$var' set as '$varval' as part of " \
			    "STEF_REGFILE_LOCAL_VARS" \
			    "defined in $STEF_CONFIG must be an absolute path."
			printf "\nPlease fix it before trying to re-run.  "
			printf "Exiting.\n"
			exit 1
		fi

		printf "\n%s%s\n%s\n" \
		    "Variable '$var' set as '$varval' as part of " \
		    "STEF_REGFILE_LOCAL_VARS" \
		    "defined in $STEF_CONFIG does not point to a regular file."
		printf "\nPlease fix it before trying to re-run.  Exiting.\n"
		exit 1
	done
fi

typeset varname
for varname in STEF_CONFIGURE STEF_UNCONFIGURE; do
	typeset varvalue=$(eval echo \$$varname)

	[[ -z $varvalue ]] && continue

	if [[ ! -x $varvalue ]]; then
		echo "Error: $varname set to '$varvalue' but not executable."
		echo "Exiting."
		exit 1
	fi
done

if [[ -n $STEF_CONFIGURE ]]; then
	printf -- "\n--- [ Configuration Start ] ---\n"
	$STEF_CONFIGURE
	if (($? != 0)); then
		echo "Configuration failed, fix it and rerun.  Exiting."
		exit 1
	fi

	printf -- "--- [ Configuration End ] ---\n"
fi

# Test must match a pattern "test-*.sh".  All other scripts are ignored.
# E.g. test-001.sh, test-002.sh, test-cmd-003, etc.
if (( $# > 0 )); then
	testnames=$*
	# Make sure all test names represent valid test scripts.
	for i in $names; do
		[[ -x test-$i.sh ]] || \
		    { echo "$i not a valid test.  Exiting." && exit 1; }
	done
else
	testfiles=$( $LS test-*.sh )
	if (( $? != 0 )); then
		echo "No valid tests present.  Exiting."
		exit 1
	fi
	testnames=$( echo "$testfiles" | cut -f2- -d- | cut -f1 -d. )
fi

printf -- "\n---[ Running tests ] ---\n"

for i in $testnames; do
	# Print the test number.
	printf "  $i\t"

	./test-$i.sh >$output 2>&1
	ret=$?

	# Go through some selected exit codes that has special meaning to STEF.
	if ((ret == STEF_UNSUPPORTED)); then
		echo "UNSUPPORTED"
		catoutput
		rm -f $output
		continue;
	elif ((ret == STEF_UNTESTED)); then
		echo "UNTESTED"
		# An untested test is a red flag as we failed even before
		# testing what we were supposed to.
		((++untested))
		catoutput
		rm -f $output
		continue;
	fi

	# Anything else aside from 0 is a test fail.
	if ((ret != 0)); then
		echo "FAIL (return code $ret)"
		((++fail))
		if [[ -s $output ]]; then
			echo "--- 8< BEGIN output ---"
			cat $output
			echo "--- 8< END output ---"
		fi
		rm $output
		continue
	fi

	# If the expected output file does not exist, we consider the test
	# successful and are done.
	if [[ ! -f test-output-$i.txt ]]; then
		echo "PASS"
		rm -f $output
		continue
	fi

	# As both stdout and stderr output goes to the same file, the unit test
	# output file must contains both expected stdout and stderr.
	diff -u test-output-$i.txt $output > $diffout
	if (($? != 0)); then
		echo "FAIL"
		((++fail))
		echo "--- 8< BEGIN diff output ---"
		cat $diffout
		echo "--- 8< END diff output ---"
	else
		echo "PASS"
	fi

	rm -f $output $diffout
done

printf -- "---[ Tests finished ] ---\n"

typeset -i stefret=0
((fail > 0)) && stefret=1
((untested > 0)) && stefret=1

if [[ -n $STEF_UNCONFIGURE ]]; then
	printf -- "\n--- [ Unconfiguration Start ] ---\n"
	if [[ -n $STEF_UNCONFIGURE_NEVER ]]; then
		echo "Skipping unconfiguration (STEF_UNCONFIGURE_NEVER)."
	elif [[ $stefret -ne 0 && -z $STEF_UNCONFIGURE_ALWAYS ]]; then
		echo "Skipping unconfiguration due to some test failures."
	else
		((stefret != 0)) &&
		    echo "Forcing unconfiguration (STEF_UNCONFIGURE_ALWAYS)."
		$STEF_UNCONFIGURE
		if (($? != 0)); then
			echo "WARNING: Unconfiguration failed."
		fi
	fi
	printf -- "--- [ Unconfiguration End ] ---\n"
fi

printf "\n=== [ $STEF_TESTSUITE_NAME Results ] ===\n"
((fail > 0)) && echo "WARNING: $fail test(s) FAILED !!!"
((untested > 0)) && echo "WARNING: $untested test(s) UNTESTED !!!"
((fail == 0 && untested == 0)) && echo "All tests passed."

exit $stefret

