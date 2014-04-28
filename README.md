About the project
=================

libevfibers is a small C fiber library that uses libev based event loop and libcoro based coroutine context switching. As libcoro alone is barely enough to do something useful, this project aims at building a complete fiber API around it while leveraging libev's high performance and flexibility.

You may ask why yet another fiber library, there are GNU Pth, State threads, etc. When I was looking at their API, I found it being too restrictive: you cannot use other event loop. For GNU Pth it's solely select based implementation, as for state threads --- they provide several implementations including poll, epoll, select though event loop is hidden underneath the public API and is not usable directly. I found another approach more sensible, namely: just put fiber layer on top of well-known and robust event loop implementation.

So what's so cool about fibers? Fibers are user-space threads. User-space means that context switching from one fiber to an other fiber takes no effort from the kernel. There are different ways to achieve this, but it's not relevant here since libcoro already does all the dirty job. At top level you have a set of functions that execute on private stacks that do not intersect. Whenever such function is going to do some blocking operation, i.e. socket read, it calls fiber library wrapper, that asks event loop to transfer execution to this function whenever some data arrives, then it yields execution to some other fiber. From the function's point of view it runs in exclusive mode and blocks on all operations, but really other such functions execute while this one is waiting. Typically most of them are waiting for something and event loop dispatches the events.

This approach helps a lot. Imagine that you have some function that requires 3 events. In classic asynchronous model you will have to arrange your function in 3 callbacks and register them in the event loop. On the other hand having one function waiting for 3 events in ``blocking'' fashion one after one is both more readable and maintainable.

Then why use event loop when you have fancy callback-less fiber wrappers? Sometimes you just need a function that will set a flag in some object when a timer times out. Creating a fiber solely for this simple task is a bit awkward.

libevfibers allows you to use fiber style wrappers for blocking operations as well as fall back to usual event loop style programming when you need it.

Recent changes have brough you (yet unreleased) libeio wrapper support, which enables you to wrap all blocking POSIX API, such as file I/O, into non-blocking fiber calls. Additionally you can wrap any 3rd-party library (such as libcurl for example) with eio custom request and integrate the library into you fiber-enabled application.

More information can be found on the [web site](http://lupus.github.io/libevfibers).

Downloading and building
========================

There are tagged releases, but generally master should be stable, so I will provide download and build instructions for building master branch.

There are some dependencies, which you need to get installed. For debian based derivatives you can use the following command:

    $ sudo apt-get install cmake libev-dev check

It is unlikely that you have libeio installed, since it's not currently officially packaged. To aid you in building libeio-enabled version, there is a fetch&build support, which requires the following additonal packages:
    
    $ sudo apt-get install cvs libtool autoconf

So now, after all the dependencies installed, you need to clone the repository:

    $ git clone git@github.com:Lupus/libevfibers.git

The library comes with handy build.sh script, which handles cmake invocation and appropriate flags. So in case you have libeio installed on your system, the following should do the work:

    $ ./build.sh

In case you dont, use the following command instead:

    $ ./build.sh +eioe

This will checkout libeio from CVS and build it in-place.

Who uses this project
=====================

Although libevfibers is a relatively young project, it is currently used in production at NVIDIA, inside of its internal server software products.

If you are using libevfibers in your environment, I would be glad to hear from you (see "Questions and feedback below")!

Running unit tests
==================

libevfibers come with extensive test suite. In order to run it, you need to do the following, given that you have built the library using the instructions above:

    $ ./build/test/evfibers_test

Recently I have integrated Travis for unit testing of each commit.

Here is the current status for master branch: [![Build Status](https://travis-ci.org/Lupus/libevfibers.png?branch=master)](https://travis-ci.org/Lupus/libevfibers)

Documentation
=============

The main header file has all public symbols documented with Doxygen.
A snapshot of documentation is eventually generated for master branch and can be viewed [online](http://olkhovskiy.me/libevfibers/docs/current/index.html).

A simple example can be found [in the source tree](https://github.com/Lupus/libevfibers/blob/master/examples/useless_time_server.c).

Questions and feedback
======================

If you have any questions or suggestions --- feel free to use [project mailing list](http://www.freelists.org/list/libevfibers).

If you happen to encounter a bug, please create a GitHub issue.

Thanks for your interest in libevfibers!
