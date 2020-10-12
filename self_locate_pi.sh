#!/bin/bash
make self_locate_pi
LOGFILE=self_locate_pi.log
while true; do
	CHECKPOINT=`( echo 'checkpoint: 0'; cat ${LOGFILE} ) | grep 'checkpoint:' | tail -n 1 | cut -d ' ' -f 2`
	echo '--8<--' `date` >> ${LOGFILE}
	./self_locate_pi $((CHECKPOINT - 100)) 2>&1 | tee -a ${LOGFILE}
	sleep 2
done

