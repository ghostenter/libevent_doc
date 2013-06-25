#### 异步I/O的简单介绍

---
大多数程序猿都是从阻塞I/O编程开始。如果I/O调用是_synchronous_, 当你调用他的时候，他会等到所有的操作都完成或者是超出了你设置的超时时间之后才会返回。例如，当你的应用调用connect()建立一个TCP连接，你的操作系统发送一个SYN包给主机，直到收到主机返回的SYN ACK包或者是连接超时你的应用才可能继续往下运行。

以下是一个使用阻塞客户端的示例。他与www.google.com建立连接，发送一个简单的HTTP请求，然后输出响应。

######[示例：A simple blocking HTTP client]()

    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <netdb.h>

    #include <unistd.h>
    #include <string.h>
    #include <stdio.h>

    int main(int c, char **v)
    {
        const char query[] =
            "GET / HTTP/1.0\r\n"
            "Host: www.google.com\r\n"
            "\r\n";
        const char hostname[] = "www.google.com";
        struct sockaddr_in sin;
        struct hostent *h;
        const char *cp;
        int fd;
        ssize_t n_written, remaining;
        char buf[1024];

        /* Look up the IP address for the hostname.   Watch out; this isn't
           threadsafe on most platforms. */
        h = gethostbyname(hostname);
        if (!h) {
            fprintf(stderr, "Couldn't lookup %s: %s", hostname, hstrerror(h_errno));
            return 1;
        }
        if (h->h_addrtype != AF_INET) {
            fprintf(stderr, "No ipv6 support, sorry.");
            return 1;
        }

        /* Allocate a new socket */
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            perror("socket");
            return 1;
        }

        /* Connect to the remote host. */
        sin.sin_family = AF_INET;
        sin.sin_port = htons(80);
        sin.sin_addr = *(struct in_addr*)h->h_addr;
        if (connect(fd, (struct sockaddr*) &sin, sizeof(sin))) {
            perror("connect");
            close(fd);
            return 1;
        }

        /* Write the query. */
        /* XXX Can send succeed partially? */
        cp = query;
        remaining = strlen(query);
        while (remaining) {
          n_written = send(fd, cp, remaining, 0);
          if (n_written <= 0) {
            perror("send");
            return 1;
          }
          remaining -= n_written;
          cp += n_written;
        }

        /* Get an answer back. */
        while (1) {
            ssize_t result = recv(fd, buf, sizeof(buf), 0);
            if (result == 0) {
                break;
            } else if (result < 0) {
                perror("recv");
                close(fd);
                return 1;
            }
            fwrite(buf, 1, result, stdout);
        }

        close(fd);
        return 0;
    }

以上所有的网络调用都是阻塞的：*gethostbyname*直到获取到www.google.com的地址信息或是失败了才会返回；*connect*直到连接之后才返回；*recv*直到收到数据或是关闭之后才会返回；*send*至少要等到所有的数据到flush到kernel的写缓冲区之后才会返回。

到现在为止，阻塞I/O还不算太糟。如果你在这段时间不需要你的程序做其他的事情的话，阻塞I/O会工作的很好。但是假设你的程序需要同时处理多个连接，举一个具体的示例：假设你同时从两台主机读取数据，但是你又不知道哪个主机的数据先到达。你不能像下面这么写：

###### Bad Example
	/* This won't work. */
	char buf[1024];
	int i, n;
	while(i_still_want_to_read()){
		n = recv(fd[i], buf, sizeof(buf), 0);
		if(n == 0){
			handle_close(fd[i]);
		}else if(n < 0){
			handle_error(fd[i], errno);
		} else{
			handle_input(fd[i], buf, n);
		}
	}
	
因为如果fd[2]上面的数据先到达，你的程序将不会尝试着从fd[2]读取数据直到fd[0]和fd[1]的数据已经获取或是结束。

有些程序猿会采用多线程或是多进程来解决这个问题。最简单的方法是采用多进程，每个进程处理一个连接。这样的话每个连接都有自己的进程，阻塞I/O调用在等待的时候不用阻塞其他的连接进程。

下面有一个简单的程序，一个简单的监听40713端口TCP连接的服务器，一次从输入端读取一行数据，然后将读取到的数据经ROT13算法转换过之后在写回。该程序用的是Unix fork()为每一个连接创建一个新的进程。

######示例：Forking ROT13 server

    #include <netinet/in.h>     // For sockaddr_in
    #include <sys/socket.h>     // For socket functions

    #include <unistd.h>
    #include <string.h>
    #include <stdio.h>
    #include <stdlib.h>

    #define MAX_LINE 16384

    char
    rot13_char(char c)
    {
        if((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M')){
            return c + 13;
        }else if((c >= 'n' && c <= 'z') || (c > 'N' && c <= 'Z')){
            return c - 13;
        }else{
            return c;
        }
    }

    void
    child(int fd)
    {
        char outbuf[MAX_LINE + 1];
        size_t outbuf_used = 0;
        ssize_t result;

        while(1){
            char ch;
            result = recv(fd, &ch, 1, 0);
            if(result == 0){
                break;
            }else if(result == -1){
                perror("read");
                break;
            }
            
            if(outbuf_used < sizeof(outbuf)){
                outbuf[outbuf_used++] = rot13_char(ch);
            }
            if(ch == '\n'){
                send(fd, outbuf, outbuf_used, 0);
                outbuf_used = 0;
                continue;
            }
        }
    }

    void
    run(void)
    {
        int listener;
        struct sockaddr_in sin;

        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = 0;
        sin.sin_port = htons(40713);

        listener = socket(AF_INET, SOCK_STREAM, 0);
    #ifndef WIN32
        {
            int one = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        }
    #endif
        if(bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0){
            perror("bind");
            return;
        }

        if(listen(listener, 16) < 0){
            perror("listen");
            return;
        }

        while(1){
            struct sockaddr_storage ss;
            socklen_t slen = sizeof(ss);
            int fd = accept(listener, (struct sockaddr*)&ss, &slen);
            if(fd < 0){
                perror("accept");
            } else {
                if(fork() == 0) {
                    child(fd);
                    exit(0);
                }
            }
        }
    }

    int
    main(int c, char **v)
    {
        run();
        return 0;
    }

因此，我们立刻就完美的解决了处理多连接的问题了吗？我们就可以停止写这本书然后去做其他的事情了吗？答案是否定的。首先，进程的创建(即使线程的创建)在某些平台是非常花费时间的。实际上，你会选择使用线程池来替代每次都创建新的进程。但是更为现实的是，线程不能按你喜好随意创建。如果你的程序需要同时处理上千或是上万个连接，每个_CPU_处理上万个线程没有处理几个线程高效。

但是如果线程不是最好的处理多线程的解决方法，那时什么呢？在类*Unix*系统中，你可以设置sockets为*nonblocking(非阻塞)*，你可以按照如下方式设置：
	
	fcntl(fd, F_SETFL, O_NONBLOCK);

fd是socket的文件描述符。一旦你设置fd(socket)为*noblocking(非阻塞)*，从这个时候开始，无论你什么时候调用网络接口都将会立即完成操作或是返回一个用力来指示像“我现在不能创建进程，请重新尝试”之类的特殊错误码。因此，以上两个实例可以简单的写成如下方式：

###### 不好的示例： busy-polling all sockets
	int i, n;
	char buf[1024];
	for(i; i < n_sockets; ++i)
		fcntl(fd[i], F_SETFL, O_NONBLOCK);
	
	while(i_still_want_to_read()){
		for(i; i < n_sockets; ++i){
			n = recv(fd[i], buf, sizeof(buf), 0);
			if(n == 0){
				handle_close(fd[i]);
			} else if(n < 0){
				if (errno == EAGAIN ){
					; /* The Kernel didn't, have any data for us to read. */
				} else {
					handler_error(fd[i], errno);
				}
			} else {
				handler_input(fd[i], buf, n);
			}
		}
	}
现在我们正在使用*nonblocking(非阻塞)*sockets, 上面的代码将会工作，仅仅勉强工作。他的性能非常的糟糕，有两个原因。第一， 
循环会无限的循环读取每个连接, 即使没有一个连接有数据可读。第二，如果你使用这种方式处理一个或两个连接，你要为每一个连接调用
系统调用接口无论是否有数据需要处理。因此我们需要的是一种方法去告诉内核“等待直到有一个socket将会给我数据，和哪一个socket已经准备好了”。

用来解决此问题的比较古老的方法是使用*select()*。*select()*调用需要3个fds集合（使用bit数组实现），分别是：读， 写， 异常，他将会等待直到其中任何一个集合准备好， 改变集合只包含可以使用的集合。

以下有一个使用*select*的示例：

###### 示例： Using select

	/* if you only hava a couple dozen fds, this version won't be awful */
	fd_set readset;
	int i, n
	char buf[1024];
	
	while(i_still_want_to_read()){
		int maxfd = -1;
		FD_ZERO(&readset);
		
		for(i=0; i<n_sockets, ++i){
			if(fd[i] > maxfd) maxfd = fd[i];
			FD_SET(fd[i], &readset);
		}	
		
		/* Wait until one or more fds are ready to read */
		select(maxfd + 1, &readset, NULL, NULL, NULL);
		
		/* Process all of the fds that are still set in readset */
		for(i = 0; i < n_socket; ++i){
			if(FD_ISSET(fd[i], &readset)){
				n = recv(fd[i], buf, sizeof(buf), 0);
				if(n == 0){
					handle_close(fd[i]);
				} else if(n < 0){
					if(errno == EAGAIN){
						;/*The kernel didn't hava any data for us to read. */
					} else{
						handle_error(fd[i], errno);
					}
				} else{
					handle_input(fd[i], buf, n);
				}
			}
		}
		
	}

下面有一个使用*select()*重新实现的 _ROT13_ SERVER.

###### 示例：select()-base ROT13 server

    #include <netinet/in.h>         // For sockaddr_in
    #include <sys/socket.h>         // For socket functions
    #include <fcntl.h>              // For fcntl
    #include <sys/select.h>


    #include <assert.h>
    #include <unistd.h>
    #include <string.h>
    #include <stdlib.h>
    #include <stdio.h>
    #include <errno.h>

    #define MAX_LINE 16384

    char
    rot13_char(char c)
    {
        if((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M')){
            return c + 13;
        }else if((c >= 'n' && c <= 'z') || (c > 'N' && c <= 'Z')){
            return c - 13;
        }else{
            return c;
        }
    }

    struct fd_state {
        char buffer[MAX_LINE];
        size_t buffer_used;

        int writing;
        size_t n_written;
        size_t write_upto;
    };

    struct fd_state *
    alloc_fd_state(void)
    {
        struct fd_state *state = malloc(sizeof(struct fd_state));
        if(!state){
            return NULL;
        }

        state->buffer_used = state->n_written = state->writing =
            state->write_upto = 0;
        return state;
    }

    void 
    free_fd_state(struct fd_state *state)
    {
        free(state);
    }

    void 
    make_nonblocking(int fd)
    {
        fcntl(fd, F_SETFL, O_NONBLOCK);
    }

    int
    do_read(int fd, struct fd_state *state)
    {
        char buf[1024];
        int i;
        ssize_t result;

        while(1){
            result = recv(fd, buf, sizeof(buf), 0);
            if(result <= 0){
                break;
            }

            for(i=0; i<result; ++i){
                if(state->buffer_used < sizeof(state->buffer)){
                    state->buffer[state->buffer_used++] = rot13_char(buf[i]);
                }

                if(buf[i] == '\n'){
                    state->writing = 1;
                    state->write_upto = state->buffer_used;
                }
            }
        }

        if(result == 0){
            return 1;
        }else if(result < 0){
            if(errno = EAGAIN){
                return 0;
            }
            return -1;
        }

        return 0;
    }

    int
    do_write(int fd, struct fd_state *state)
    {
        while(state->n_written < state->write_upto){
            ssize_t result = send(fd, state->buffer + state->n_written,
                    state->write_upto-state->n_written, 0);
            if(result < 0){
                if(errno == EAGAIN){
                    return 0;
                }
                return -1;
            }

            assert(result != 0);
            state->n_written += result;
        }

        if(state->n_written == state->buffer_used){
            state->n_written = state->write_upto = state->buffer_used = 0;
        }

        state->writing = 0;
        return 0;
    }

    void
    run(void)
    {
        int listener;
        struct fd_state *state[FD_SETSIZE];
        struct sockaddr_in sin;

        int i, maxfd;
        fd_set readset, writeset, exset;

        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = 0;
        sin.sin_port = htons(40713);

        for(i=0; i<FD_SETSIZE;++i){
            state[i] = NULL;
        }

        listener = socket(AF_INET, SOCK_STREAM, 0);
        make_nonblocking(listener);

    #ifndef WIN32
        {
            int one = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        } 
    #endif
        
        if(bind(listener, (struct sockaddr *)&sin, sizeof(sin)) < 0){
            perror("blind");
            return;
        }

        if(listen(listener, 16) < 0){
            perror("listen");
            return;
        }

        FD_ZERO(&readset);
        FD_ZERO(&writeset);
        FD_ZERO(&exset);

        while(1){
            maxfd = listener;

            FD_ZERO(&readset);
            FD_ZERO(&writeset);
            FD_ZERO(&exset);

            FD_SET(listener, &readset);
            for(i=0; i<FD_SETSIZE; ++i){
                if(state[i]){
                    if(i > maxfd){
                        maxfd = i;
                    }
                    FD_SET(i, &readset);
                    if(state[i]->writing){
                        FD_SET(i, &writeset);
                    }
                }
            }

            if(select(maxfd+1, &readset, &writeset, &exset, NULL)<0){
                perror("select");
                return;
            }
            
            if(FD_ISSET(listener, &readset)){
                struct sockaddr_storage ss;
                socklen_t slen = sizeof(ss);
                int fd = accept(listener, (struct sockaddr *)&ss, &slen);
                if(fd < 0){
                    perror("accept");
                }else if(fd > FD_SETSIZE){
                    close(fd);
                }else{
                    make_nonblocking(fd);
                    state[fd] = alloc_fd_state();
                    assert(state[fd]);
                }
            }

            for(i=0; i< maxfd + 1; ++i){
                int r = 0;
                if(i == listener)
                    continue;
                if(FD_ISSET(i, &readset)){
                    r = do_read(i, state[i]);
                }
                if(r==0 && FD_ISSET(i, &writeset)){
                    r = do_write(i, state[i]);
                }
                if(r){
                    free_fd_state(state[i]);
                    state[i] = NULL;
                    close(i);
                }
            }
        }
    }

    int 
    main(int c, char **v)
    {
        setvbuf(stdout, NULL, _IONBF, 0);
        run();
        return 0;
    }

但是我们还没有完成。因为对于我们提供的最大的fd生成和读取*select()*位数组花费会花费一定比例的时间，如果socket比较大的话*select()*性能非常的糟糕。

不同的操作系统已经提供为*select*了不同的替换函数。包括poll()，epoll()，kqueue()，evports和/dev/poll。以上替代函数的性能都比*select*要好，除了poll之外其他的添加socket，删除socket和通知socket已经可以读写的时间复杂度为O(1)。

不幸的是没有一个高效的接口是支持所有平台的。Linux下有epoll()，BSD(包括Darwin)有kqueue()，Solaris有evports和/dev/poll… 没有一个操作系统有其他操作系统的接口。所以，如果你想写一个可以可移植的高性能的异步的应用程序，你要抽象封装这些接口提供其中最高效的接口。

Libevent最底层的API就是抽象封装各种接口。它为各种*select*替代函数提供一致的接口，在你的电脑上使用可用的最高效的*select*替代函数。

一下是另一个异步的ROT13 SERVER的版本。这次使用Libevent2替代select()。注意fd_sets已经移除，我们结合和分离event使用event_base结构，event_base将会从select(), poll(), epoll(), kqueue()等实现。

###### 示例： A low-level ROT13 server with Libevent

    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <fcntl.h>

    #include <event2/event.h>

    #include <assert.h>
    #include <unistd.h>
    #include <string.h>
    #include <stdlib.h>
    #include <stdio.h>
    #include <errno.h>

    #define MAX_LINE 16384

    void do_read(evutil_socket_t fd, short events, void *arg);
    void do_write(evutil_socket_t fd, short events, void *arg);

    char
    rot13_char(char c)
    {
        if((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M')){
            return c + 13;
        }else if((c >= 'n' && c <= 'z') || (c > 'N' && c <= 'Z')){
            return c - 13;
        }else{
            return c;
        }
    }

    struct fd_state{
        char buffer[MAX_LINE];
        size_t buffer_used;

        size_t n_written;
        size_t write_upto;

        struct event *read_event;
        struct event *write_event;
    };

    struct fd_state *
    alloc_fd_state(struct event_base *base, evutil_socket_t fd)
    {
        struct fd_state *state = malloc(sizeof(struct fd_state));
        if(!state){
            return NULL;
        }

        state->read_event = event_new(base, fd, EV_READ|EV_PERSIST, do_read, state);
        if(!state->read_event){
            free(state);
            return NULL;
        }

        state->write_event = event_new(base, fd, EV_WRITE|EV_PERSIST, do_write, state);
        if(!state->write_event){
            event_free(state->read_event);
            free(state);
            return NULL;
        }

        state->buffer_used = state->n_written = state->write_upto = 0;
        assert(state->write_event);
        return state;
    }

    void
    free_fd_state(struct fd_state *state)
    {
        event_free(state->read_event);
        event_free(state->write_event);
        free(state);
    }

    void
    do_read(evutil_socket_t fd, short events, void *arg)
    {
        struct fd_state *state = arg;
        char buf[1024];
        int i;
        ssize_t result;

        while(1){
            assert(state->write_event);
            result = recv(fd, buf, sizeof(buf), 0);
            if(result <= 0){
                break;
            }

            for(i=0; i<result; ++i){
                if(state->buffer_used < sizeof(state->buffer)){
                    state->buffer[state->buffer_used++] = rot13_char(buf[i]);
                }
                if(buf[i] == '\n'){
                    assert(state->write_event);
                    event_add(state->write_event, NULL);
                    state->write_upto = state->buffer_used;
                }
            }
        }

        if(result == 0){
            free_fd_state(state);
        }else if(result < 0){
            if(errno == EAGAIN){
                return;
            }
            perror("recv");
            free_fd_state(state);
        }
    }

    void
    do_write(evutil_socket_t fd, short events, void *arg)
    {
        struct fd_state *state = arg;
        
        while(state->n_written < state->write_upto){
            ssize_t result = send(fd, state->buffer + state->n_written,
                    state->write_upto - state->n_written, 0);
            if(result < 0){
                if(errno == EAGAIN){
                    return;
                }
                free_fd_state(state);
                return;
            }

            assert(result != 0);
            state->n_written += result;
        }
        
        if(state->n_written == state->buffer_used){
            state->n_written = state->write_upto = state->buffer_used = 1;
        }
        event_del(state->write_event);
    }

    void
    do_accept(evutil_socket_t listener, short event, void *arg)
    {
        struct event_base *base = arg;
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);

        int fd = accept(listener, (struct sockaddr *)&ss, &slen);
        if(fd < 0){
            perror("accept");
        }else if(fd > FD_SETSIZE){
            close(fd);
        }else{
            struct fd_state *state;
            evutil_make_socket_nonblocking(fd);
            state = alloc_fd_state(base, fd);
            assert(state);
            assert(state->write_event);
            event_add(state->read_event, NULL);
        }
    }

    void
    run(void)
    {
        evutil_socket_t listener;
        struct sockaddr_in sin;
        struct event_base *base;
        struct event *listener_event;

        base = event_base_new();
        if(!base){
            return;
        }

        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = 0;
        sin.sin_port = htons(40713);

        listener = socket(AF_INET, SOCK_STREAM, 0);
        evutil_make_socket_nonblocking(listener);

    #ifndef WIN32
        {
            int one = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        }

    #endif
        if(bind(listener, (struct sockaddr *)&sin, sizeof(sin)) < 0){
            perror("bind");
            return;
        }

        if(listen(listener, 16) < 0){
            perror("listene");
            return;
        }

        listener_event = event_new(base, listener, EV_READ|EV_PERSIST, do_accept, (void*)base);
        event_add(listener_event, NULL);
        event_base_dispatch(base);
    }

    int
    main(int c, char **v)
    {
        setvbuf(stdout, NULL, _IONBF, 0);

        run();
        return 0;
    }


(另一个需要注意的是，socket的类型使用*evutil_socket_t*替换了原来的*int*, 使用*evutil_make_socket_nonblocking*代替*fcntl(O_NONBLOCK)*来设置socket为非阻塞的。这些改变让我们的代码兼容WIN32网络接口的差异部分。)


#### 开发效率怎么样呢?(Windows下呢)

---

你可能主要到了尽管我们的代码的效率变得更高效了，但是代码却更加复杂了。回到我们使用fork的示例中，我们不需要为每一个连接管理buffer: 我们仅仅为每一个进程在堆上申请了buffer。我们不需要精确的跟踪每一个socket是否正在读或是写：这在我们的代码中是不清晰的。我们不需要一个结构体来跟踪有多少操作已经完成：我们仅仅只用了循环和堆栈变量。

等多的是，如果你对win32网络编程有一定的了解，你会意识到上述的示例使用Libevent性能可能没有达到最理想的状态。在Windows上，快速的异步IO并不是使用select()，而是类似的ICOP(IO Completion Ports)接口。不像所有的快速网络接口，IOCP病不是判断你的程序中socket什么时候准备好读写然后你的程序去执行，而是程序告诉Windows网络堆开始执行网络操作，当操作完成之后IOCP告诉在告诉程序。

幸运的是，Libevent2 "bufferevents"接口解决了这些问题：他让程序更容易编写，提供的接口在Window和Unix上都可以高效的实现。

下面有一个ROT13 SERVER的最终版，使用的是bufferevents接口。

###### 示例： A simpler ROT13 server with libevent

    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <fcntl.h>

    #include <event2/event.h>
    #include <event2/buffer.h>
    #include <event2/bufferevent.h>

    #include <assert.h>
    #include <unistd.h>
    #include <string.h>
    #include <stdlib.h>
    #include <stdio.h>
    #include <errno.h>

    #define MAX_LINE 16384

    char
    rot13_char(char c)
    {
        if((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M')){
            return c + 13;
        }else if((c >= 'n' && c <= 'z') || (c > 'N' && c <= 'Z')){
            return c - 13;
        }else{
            return c;
        }
    }

    void
    readcb(struct bufferevent *bev, void *ctx)
    {
        struct evbuffer *input, *output;
        char *line;
        size_t n;
        int i;
        input = bufferevent_get_input(bev);
        output = bufferevent_get_output(bev);

        while((line = evbuffer_readln(input, &n, EVBUFFER_EOL_LF))){
            for(i=0; i<n; ++i){
                line[i] = rot13_char(line[i]);
            }

            evbuffer_add(output, line, n);
            evbuffer_add(output, "\n", 1);
            free(line);
        }
        
        if(evbuffer_get_length(input) > MAX_LINE){
            char buf[1024];
            while(evbuffer_get_length(input)){
                int n = evbuffer_remove(input, buf, sizeof(buf));
                for(i=0; i<n; i++){
                    buf[i] = rot13_char(buf[i]);
                }
                evbuffer_add(output, buf, n);
            }
            evbuffer_add(output, "\n", 1);
        }
    }

    void
    errorcb(struct bufferevent *bev, short error, void *ctx)
    {
        if(error & BEV_EVENT_EOF){
        }else if(error & BEV_EVENT_ERROR){
        }else if(error & BEV_EVENT_TIMEOUT){
        }
        bufferevent_free(bev);
    }

    void
    do_accept(evutil_socket_t listener, short event, void *arg)
    {
        struct event_base *base = arg;
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int fd = accept(listener, (struct sockaddr*)&ss, &slen);
        if(fd < 0){
            perror("accept");
        }else if(fd > FD_SETSIZE){
            close(fd);
        }else{
            struct bufferevent *bev;
            evutil_make_socket_nonblocking(fd);
            bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
            bufferevent_setcb(bev, readcb, NULL, errorcb, NULL);
            bufferevent_setwatermark(bev, EV_READ, 0, MAX_LINE);
            bufferevent_enable(bev, EV_READ|EV_WRITE);
        }
    }

    void
    run(void)
    {
        evutil_socket_t listener;
        struct sockaddr_in sin;
        struct event_base *base;
        struct event *listener_event;

        base = event_base_new();
        if(!base){
            return;
        }

        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = 0;
        sin.sin_port = htons(40713);

        listener = socket(AF_INET, SOCK_STREAM, 0);
        evutil_make_socket_nonblocking(listener);

    #ifndef WIN32
        {
            int one = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        }

    #endif
        if(bind(listener, (struct sockaddr *)&sin, sizeof(sin)) < 0){
            perror("bind");
            return;
        }

        if(listen(listener, 16) < 0){
            perror("listene");
            return;
        }

        listener_event = event_new(base, listener, EV_READ|EV_PERSIST, do_accept, (void*)base);
        event_add(listener_event, NULL);
        event_base_dispatch(base);
    }

    int
    main(int c, char **v)
    {
        setvbuf(stdout, NULL, _IONBF, 0);

        run();
        return 0;
    }


#### Libevent的效率很高，是真的吗？

---

XXX写了一个关于效率的章节在这里。Libevent基准页面已经过期。

