ALL=xf_ttsc

xf_ttsc: xf_ttsc.o
	gcc  -o $@ $< -L. -lmsc -lpthread -ldl -lrt

xf_ttsc.o: xf_ttsc.c
	gcc -c -Iinclude -o $@ $<

clean: 
	@rm -f *.o xf_ttsc
