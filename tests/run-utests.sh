#/bin/bash

cd "$(dirname "${0}")"

RC=0
PCAPS=`cd pcaps; /bin/ls *.*cap*`

for f in $PCAPS; do
	OUTPUT=/tmp/$(basename -- $f).fuzzed

	./pl7m_test ./pcaps/$f $OUTPUT
	CMD_RET=$?
	if [ $CMD_RET -eq 0 ]; then
		NUM_DIFF=`diff <(xxd ./results/$f.fuzzed) <(xxd $OUTPUT) | wc -l`
		if [ $NUM_DIFF -eq 0 ]; then
			echo "$f OK"
		else
			echo "$f Diff Error"
			RC=$(( RC + 1 ))
			#diff <(xxd ./results/$f.fuzzed) <(xxd $OUTPUT)
		fi
	else
		echo "%f Error!"
		RC=$(( RC + 1 ))
	fi

done

exit $RC
