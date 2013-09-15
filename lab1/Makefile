NAME=tcp-proxy

all:
	gcc $(NAME).c -levent -o $(NAME)
dist:
	tar czvf tcp-proxy.tar.gz ./*

clean:
	rm -rf $(NAME)
