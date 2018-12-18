###############################################################################
PRGOBJS1 = \
	$(HOME)/src/httpclient.o \
	$(HOME)/src/test.o 

PRGLIBS1 = 
PRGTARG1 = testmyhttp
PRGDEFS1 = 

###############################################################################

#make命令默认是all
debug all: debugdynamic   debugstatic   debugexec
release:   releasedynamic releasestatic releaseexec
clean:     cleandynamic   cleanstatic   cleanexec


##STATIC LIB
debugstatic releasestatic cleanstatic:

##DYNAMIC LIB
#需要执行命令ldconfig 库文件存放目录，把库文件加入到/etc/ld.so.cache文件中
#或者在LD_LIBRARY_PATH加入库文件所做目录
debugdynamic releasedynamic cleandynamic:

##EXECUTABLE BIN
releaseexec   debugexec    cleanexec:
	@make -f $(HOME)/mak/mkstand.mak $@ TARGET="$(PRGTARG1)" OBJS="$(PRGOBJS1)" LIBS="$(PRGLIBS1)" DEFS="$(PRGDEFS1)"
