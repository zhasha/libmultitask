p9=`{readlink -f ~/.p9}
<$p9/$objtype/mkfile

LIB=libmultitask.a

OFILES=\
	chan.$O\
	cond.$O\
	iochan.$O\
	lock.$O\
	qlock.$O\
	queue.$O\
	ref.$O\
	rendez.$O\
	rwlock.$O\
	task-$objtype.$O\
	task.$O\
	timechan.$O\
	timequeue.$O\

HFILES=\
	multitask.h

<$p9/mklib

%.$O: multitask-impl.h
