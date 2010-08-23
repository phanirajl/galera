// Copyright (C) 2009 Codership Oy <info@codership.com>

#include <vector>
#include <deque>
#include <algorithm>
#include <functional>
#include <stdexcept>

#include <cstdlib>
#include <cstring>
#include <cassert>

#include "gu_logger.hpp"
#include "gu_uri.hpp"
#include "gu_network.hpp"
#include "gu_resolver.hpp"
#include "gu_lock.hpp"
#include "gu_prodcons.hpp"

#include "gu_net_test.hpp"

using std::vector;
using std::string;
using std::deque;
using std::mem_fun;
using std::for_each;
using namespace gu;
using namespace gu::net;
using namespace gu::prodcons;

// Assertion to replace fail_unless() with threaded tests.
#define test_assert(_a) do { if ((_a)) { } else { gu_throw_fatal << #_a ; } } while (0)

static void log_foo()
{
    log_debug << "foo func";
}

static void log_bar()
{
    log_debug << "bar func";
}

static void debug_logger_checked_setup()
{
    // gu_log_max_level = GU_LOG_DEBUG;
}

static void debug_logger_checked_teardown()
{
    // gu_log_max_level = GU_LOG_INFO;
}

START_TEST(test_debug_logger)
{
    Logger::set_debug_filter("log_foo");
    log_foo();
    log_bar();
}
END_TEST


START_TEST(test_buffer)
{
    // @todo
}
END_TEST

START_TEST(test_datagram)
{

    // Header check
    NetHeader hdr(42, 0);
    fail_unless(hdr.len() == 42);
    fail_unless(hdr.has_crc32() == false);
    fail_unless(hdr.version() == 0);

    hdr.set_crc32(1234);
    fail_unless(hdr.has_crc32() == true);
    fail_unless(hdr.len() == 42);

    NetHeader hdr1(42, 1);
    fail_unless(hdr1.len() == 42);
    fail_unless(hdr1.has_crc32() == false);
    fail_unless(hdr1.version() == 1);

    byte_t hdrbuf[NetHeader::serial_size_];
    fail_unless(serialize(hdr1, hdrbuf, sizeof(hdrbuf), 0) ==
                NetHeader::serial_size_);
    try
    {
        unserialize(hdrbuf, sizeof(hdrbuf), 0, hdr);
        fail("");
    }
    catch (Exception& e)
    {
        // ok
    }


    byte_t b[128];
    for (byte_t i = 0; i < sizeof(b); ++i)
    {
        b[i] = i;
    }
    Buffer buf(b, b + sizeof(b));

    Datagram dg(buf);
    fail_unless(dg.get_len() == sizeof(b));

    // Normal copy construction
    Datagram dgcopy(buf);
    fail_unless(dgcopy.get_len() == sizeof(b));
    fail_unless(memcmp(dgcopy.get_header() + dgcopy.get_header_offset(),
                       dg.get_header() + dg.get_header_offset(),
                       dg.get_header_len()) == 0);
    fail_unless(dgcopy.get_payload() == dg.get_payload());

    // Copy construction from offset of 16
    Datagram dg16(dg, 16);
    log_info << dg16.get_len();
    fail_unless(dg16.get_len() - dg16.get_offset() == sizeof(b) - 16);
    for (byte_t i = 0; i < sizeof(b) - 16; ++i)
    {
        fail_unless(dg16.get_payload()[i + dg16.get_offset()] == i + 16);
    }

#if 0
    // Normalize datagram, all data is moved into payload, data from
    // beginning to offset is discarded. Normalization must not change
    // dg
    dg16.normalize();

    fail_unless(dg16.get_len() == sizeof(b) - 16);
    for (byte_t i = 0; i < sizeof(b) - 16; ++i)
    {
        fail_unless(dg16.get_payload()[i] == i + 16);
    }

    fail_unless(dg.get_len() == sizeof(b));
    for (byte_t i = 0; i < sizeof(b); ++i)
    {
        fail_unless(dg.get_payload()[i] == i);
    }

    Datagram dgoff(buf, 16);
    dgoff.get_header().resize(8);
    dgoff.set_header_offset(4);
    fail_unless(dgoff.get_len() == buf.size() + 4);
    fail_unless(dgoff.get_header_offset() == 4);
    fail_unless(dgoff.get_header().size() == 8);
    for (byte_t i = 0; i < 4; ++i)
    {
        *(&dgoff.get_header()[0] + i) = i;
    }

    dgoff.normalize();

    fail_unless(dgoff.get_len() == sizeof(b) - 16 + 4);
    fail_unless(dgoff.get_header_offset() == 0);
    fail_unless(dgoff.get_header().size() == 0);
#endif // 0
}
END_TEST

START_TEST(test_resolver)
{
    std::string tcp_lh4("tcp://127.0.0.1:2002");

    Addrinfo tcp_lh4_ai(resolve(tcp_lh4));
    fail_unless(tcp_lh4_ai.get_family() == AF_INET);
    fail_unless(tcp_lh4_ai.get_socktype() == SOCK_STREAM);

    fail_unless(tcp_lh4_ai.to_string() == tcp_lh4, "%s != %s",
                tcp_lh4_ai.to_string().c_str(), tcp_lh4.c_str());

    std::string tcp_lh6("tcp://[::1]:2002");

    Addrinfo tcp_lh6_ai(resolve(tcp_lh6));
    fail_unless(tcp_lh6_ai.get_family() == AF_INET6);
    fail_unless(tcp_lh6_ai.get_socktype() == SOCK_STREAM);

    fail_unless(tcp_lh6_ai.to_string() == tcp_lh6, "%s != %s",
                tcp_lh6_ai.to_string().c_str(), tcp_lh6.c_str());


    std::string lh("tcp://localhost:2002");
    Addrinfo lh_ai(resolve(lh));
    fail_unless(lh_ai.to_string() == "tcp://127.0.0.1:2002" ||
                lh_ai.to_string() == "tcp://[::1]:2002");

}
END_TEST


START_TEST(test_network_listen)
{
    log_info << "START";
    Network net;
    Socket* listener = net.listen("tcp://localhost:2112");
    listener->close();
    delete listener;
}
END_TEST

struct listener_thd_args
{
    Network* net;
    int conns;
    const byte_t* buf;
    const size_t buflen;
};

void* listener_thd(void* arg)
{
    listener_thd_args* larg = reinterpret_cast<listener_thd_args*>(arg);
    Network* net = larg->net;
    int conns = larg->conns;
    uint64_t bytes = 0;
    const byte_t* buf = larg->buf;
    const size_t buflen = larg->buflen;

    while (conns > 0)
    {
        NetworkEvent ev = net->wait_event(-1);
        Socket* sock = ev.get_socket();
        const int em = ev.get_event_mask();

        if (em & E_ACCEPTED)
        {
            log_info << "accepted local " << sock->get_local_addr();
            log_info << "accepted remote " << sock->get_remote_addr();
        }
        else if (em & E_ERROR)
        {
            test_assert(sock != 0);
            if (sock->get_state() == Socket::S_CLOSED)
            {
                log_info << "Listener: socket closed";
                delete sock;
                conns--;
            }
            else
            {
                test_assert(sock->get_state() == Socket::S_FAILED);
                test_assert(sock->get_errno() != 0);
                log_info << "Listener: socket read failed: " << sock->get_errstr();
                sock->close();
                delete sock;
                conns--;
            }
        }
        else if (em & E_IN)
        {
            const Datagram* dm = sock->recv();
            if (dm == 0 && sock->get_state() == Socket::S_CLOSED)
            {
                delete sock;
                conns--;
            }
            else
            {
                test_assert(dm != 0);
                bytes += dm->get_len();
                if (buf != 0)
                {
                    test_assert(dm->get_len() <= buflen);
                    test_assert(memcmp(&dm->get_payload()[0], buf, 
                                       dm->get_len() - dm->get_offset()) == 0);
                }
            }
        }
        else if (em & E_CLOSED)
        {
            delete sock;
            conns--;
        }
        else if (em & E_EMPTY)
        {

        }
        else if (sock == 0)
        {
            log_error << "Listener: wut?: " << em;
            return reinterpret_cast<void*>(1);
        }
        else
        {
            log_error <<  "Listener: socket " << sock->get_fd()
                      << " event mask: " << ev.get_event_mask();
            return reinterpret_cast<void*>(1);
        }
    }
    log_info << "Listener: received " << bytes/(1 << 20) << "MB + "
             << bytes%(1 << 20) << "B";
    return 0;
}

START_TEST(test_network_connect)
{
    gu_log_max_level = GU_LOG_DEBUG;
    log_info << "START";
    Network* net = new Network;
    Socket* listener = net->listen("tcp://localhost:2112");

    log_info << "listener " << listener->get_local_addr();

    listener_thd_args args = {net, 2, 0, 0};
    pthread_t th;
    pthread_create(&th, 0, &listener_thd, &args);

    Network* net2 = new Network;
    Socket* conn = net2->connect("tcp://localhost:2112");

    fail_unless(conn != 0);
    fail_unless(conn->get_state() == Socket::S_CONNECTED);

    log_info << "connected remote " << conn->get_remote_addr();
    log_info << "connected local " << conn->get_local_addr();

    Socket* conn2 = net2->connect("tcp://localhost:2112");
    fail_unless(conn2 != 0);
    fail_unless(conn2->get_state() == Socket::S_CONNECTED);

    conn->close();
    delete conn;

    log_info << "conn closed";

    conn2->close();
    delete conn2;

    log_info << "conn2 closed";

    void* rval = 0;
    pthread_join(th, &rval);

    listener->close();
    delete listener;

    log_info << "test connect end";

    delete net;
    delete net2;

}
END_TEST

START_TEST(test_network_send)
{
    log_info << "START";
    const size_t bufsize(1 << 15);
    byte_t* buf = new byte_t[bufsize];
    for (size_t i = 0; i < bufsize; ++i)
    {
        buf[i] = static_cast<byte_t>(i);
    }

    Network* net = new Network;
    Socket* listener = net->listen("tcp://localhost:2112");
    listener_thd_args args = {net, 2, buf, bufsize};
    pthread_t th;
    pthread_create(&th, 0, &listener_thd, &args);

    Network* net2 = new Network;
    Socket* conn = net2->connect("tcp://localhost:2112");

    fail_unless(conn != 0);
    fail_unless(conn->get_state() == Socket::S_CONNECTED);

    Socket* conn2 = net2->connect("tcp://localhost:2112");
    fail_unless(conn2 != 0);
    fail_unless(conn2->get_state() == Socket::S_CONNECTED);

    try
    {
        vector<byte_t> toobig(Network::get_mtu() + 1, 0);
        Datagram dg(Buffer(toobig.begin(), toobig.end()));
        conn->send(&dg);
        fail("exception not thrown");
    } 
    catch (Exception& e)
    {
        log_info << e.what();
        fail_unless(e.get_errno() == EMSGSIZE);
    }

    size_t sent(0);

    for (int i = 0; i < 1000; ++i)
    {
        size_t dlen = std::min(bufsize, static_cast<size_t>(1 + i*11));
        Datagram dm(Buffer(buf, buf + dlen));
        // log_info << "sending " << dlen;
        if (i % 100 == 0)
        {
            log_debug << "sending " << dlen;
        }
        int err = conn->send(&dm);
        if (err != 0)
        {
            log_info << err;
        }
        else
        {
            sent += dlen;
        }
    }

#if 0
    for (int i = 0; i < 1000; ++i)
    {
        size_t dlen(std::min(bufsize, static_cast<size_t>(1 + i*11)));
        size_t hdrsize(8);
        size_t hdroff(dlen > hdrsize ? (rand() % 9) : 8);

//        log_info << hdrsize << " " << hdroff << " " << dlen;
//        log_info << reinterpret_cast<void*>(buf + (hdrsize - hdroff)) << " " 
//                 << reinterpret_cast<void*>(buf + dlen - (hdrsize - hdroff));
        Datagram dm(Buffer(buf + (hdrsize - hdroff), buf + dlen));
        if (hdroff < hdrsize)
        {
            dm.get_header().resize(hdrsize);
            dm.set_header_offset(hdroff);
            std::copy(buf, buf + (hdrsize - hdroff), dm.get_header().begin() + hdroff);
        }
        // log_info << "sending " << dlen;
        if (i % 100 == 0)
        {
            log_debug << "sending " << dlen;
        }
        int err = conn->send(&dm);
        if (err != 0)
        {
            log_info << err;
        }
        else
        {
            sent += dlen;
        }
    }
#endif // 0

    log_info << "sent " << sent;
    conn->close();
    delete conn;

    conn2->close();
    delete conn2;

    void* rval = 0;
    pthread_join(th, &rval);

    listener->close();
    delete listener;

    delete net;
    delete net2;

    delete[] buf;

}
END_TEST

void* interrupt_thd(void* arg)
{
    Network* net = reinterpret_cast<Network*>(arg);
    NetworkEvent ev = net->wait_event(-1);
    test_assert(ev.get_event_mask() & E_EMPTY);
    return 0;
}

START_TEST(test_network_interrupt)
{
    log_info << "START";
    Network net;
    pthread_t th;
    pthread_create(&th, 0, &interrupt_thd, &net);

    sleep(1);

    net.interrupt();

    pthread_join(th, 0);

}
END_TEST

static void make_connections(Network& net, 
                             vector<Socket*>& cl,
                             vector<Socket*>& sr,
                             size_t n)
{
    cl.resize(n);
    sr.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        cl[i] = net.connect("tcp://localhost:2112?socket.non_blocking=1");
    }
    size_t sr_cnt = 0;
    size_t cl_cnt = 0;
    do
    {
        NetworkEvent ev = net.wait_event(-1);
        const int em = ev.get_event_mask();
        if (em & E_ACCEPTED)
        {
            log_info << "accepted";
            sr[sr_cnt++] = ev.get_socket();
        }
        else if (em & E_CONNECTED)
        {
            log_info << "connected";
            cl_cnt++;
        }
        else
        {
            log_warn << "unhandled event " << em;
        }
    }
    while (sr_cnt != n || cl_cnt != n);
}


static void close_connections(Network& net, 
                              vector<Socket*> cl,
                              vector<Socket*> sr)
{
    for_each(cl.begin(), cl.end(), mem_fun(&Socket::close));
    size_t cnt = 0;
    while (cnt != sr.size())
    {
        NetworkEvent ev = net.wait_event(-1);
        const int em = ev.get_event_mask();
        Socket* sock = ev.get_socket();
        if (em & E_CLOSED)
        {
            cnt++;
        }
        else if (em & E_ERROR)
        {
            log_warn << "error: " << sock->get_errstr();
            cnt++;
        }
        else
        {
            log_debug << "unhandled events: " << em;
        }
    }
}


START_TEST(test_network_nonblocking)
{
    log_info << "START";
    Network net;

    Socket* listener = net.listen("tcp://localhost:2112?socket.non_blocking=1");
    log_info << "listener: " << *listener;
    vector<Socket*> cl;
    vector<Socket*> sr;
    gu_log_max_level = GU_LOG_DEBUG;
    make_connections(net, cl, sr, 3);

    close_connections(net, cl, sr);

    for_each(cl.begin(), cl.end(), DeleteObject());
    for_each(sr.begin(), sr.end(), DeleteObject());

    listener->close();
    delete listener;
}
END_TEST


class Thread
{
    volatile bool interrupted;
    pthread_t self;
protected:
    virtual void run() = 0;
public:
    Thread() : 
        interrupted(false),
        self()
    {
    }

    virtual ~Thread()
    {
    }


    virtual void interrupt()
    {
        interrupted = true;
        pthread_cancel(self);
    }

    bool is_interrupted() const
    {
        return interrupted;
    }

    static void start_fn(Thread* thd)
    {
        thd->run();
        pthread_exit(0);
    }

    void start()
    {
        int err = pthread_create(&self, 0, 
                                 reinterpret_cast<void* (*)(void*)>(&Thread::start_fn), this);
        if (err != 0)
        {
            gu_throw_error(err) << "could not start thread";
        }
    }

    void stop()
    {
        interrupt();
        int err = pthread_join(self, 0);

        if (err != 0)
        {
            gu_throw_error(err) << "could not join thread";
        }
    }
};

class MsgData : public MessageData
{
public:
    MsgData(const byte_t* data_, size_t data_size_) :
        data(data_),
        data_size(data_size_)
    { }
    const byte_t* get_data() const { return data; }
    size_t get_data_size() const { return data_size; }
    MsgData(const MsgData& md) :
        MessageData(),
        data(md.data),
        data_size(md.data_size)
    { }
    MsgData& operator=(const MsgData& md)
    {
        data = md.data;
        data_size = md.data_size;
        return *this;
    }
private:
    const byte_t* data;
    size_t data_size;
};

class NetConsumer : public Consumer, public Thread
{
    Network net;
    Socket* listener;
    Socket* send_sock;

    NetConsumer(const NetConsumer&);
    void operator=(const NetConsumer&);

public:

    void connect(const string& url)
    {
        send_sock = net.connect(url);

        while (true)
        {
            NetworkEvent ev = net.wait_event(-1);
            const int em = ev.get_event_mask();
            Socket* sock = ev.get_socket();

            if (em & E_ACCEPTED)
            {
            }
            else if (em & E_CONNECTED)
            {
                test_assert(sock == send_sock);
                log_info << "connected";
                break;
            }
            else
            {
                gu_throw_fatal << "Unexpected event mask: " << em;
            }
        }
    }

    void close()
    {
        send_sock->close();
    }

    NetConsumer(const string& url) :
        net(),
        listener(net.listen(url)),
        send_sock(0)
    {
    }

    ~NetConsumer()
    {
        delete listener;
        delete send_sock;
    }

    void notify()
    {
        net.interrupt();
    }

    void run()
    {
        size_t sent = 0;
        size_t recvd = 0;
        while (is_interrupted() == false)
        {
            const Message* msg = get_next_msg();

            if (msg != 0)
            {
                const MsgData* md(reinterpret_cast<const MsgData*>(msg->get_data()));
                const Datagram dg(Buffer(md->get_data(), 
                                         md->get_data() + md->get_data_size()));

                int err = send_sock->send(&dg);
                if (err != 0)
                {
                    // log_warn << "send: " << strerror(err);
                }
                sent += dg.get_len();
                Message ack(&msg->get_producer(), 0, err);
                return_ack(ack);

            }

            NetworkEvent ev = net.wait_event(-1);
            const int em = ev.get_event_mask();
            Socket* sock = ev.get_socket();
            if (em & E_IN)
            {
                const Datagram* dg = sock->recv();
                if (dg == 0 && sock->get_state() == Socket::S_CLOSED)
                {
                    delete sock;
                }
                else
                {
                    test_assert(dg != 0);
                    recvd += dg->get_len() - dg->get_offset();
                    test_assert(recvd <= sent);
                }
            }
            else if (em & E_CLOSED)
            {
                delete sock;
            }
            else if (em & E_ERROR)
            {
                sock->close();
                delete sock;
            }
            else if (em & E_EMPTY)
            {
                /* */
            }
            else
            {
                log_warn << "unhandled event: " << em;
            }
        }
    }

};

START_TEST(test_net_consumer)
{
    log_info << "START";
    string url("tcp://localhost:2112?socket.non_blocking=1");
    NetConsumer cons(url);
    cons.connect(url);

    cons.start();

    Producer prod(cons);
    byte_t buf[128];
    memset(buf, 0xab, sizeof(buf));
    for (size_t i = 0; i < 1000; ++i)
    {
        if (i % 100 == 0)
        {
            log_debug << "iter " << i;
        }
        // Datagram dg(Buffer(buf, buf + sizeof(buf)));
        MsgData md(buf, sizeof(buf));
        Message msg(&prod, &md);
        Message ack(&prod);
        prod.send(msg, &ack);
        fail_unless(ack.get_val() == 0 || ack.get_val() == EAGAIN);
    }
    log_debug << "stopping";
    cons.stop();
    cons.close();
}
END_TEST

struct producer_thd_args
{
    Consumer& cons;
    size_t n_events;
    pthread_barrier_t barrier;
    producer_thd_args(Consumer& cons_, size_t n_events_,
                      unsigned int n_thds_) :
        cons(cons_),
        n_events(n_events_),
        barrier()
    {
        int err = pthread_barrier_init(&barrier, 0, n_thds_);

        if (err) gu_throw_error(err) << "Could not initialize barrier";
    }
};

void* producer_thd(void* arg)
{
    producer_thd_args* pargs = reinterpret_cast<producer_thd_args*>(arg);

    byte_t buf[128];
    memset(buf, 0xab, sizeof(buf));
    Producer prod(pargs->cons);
    int ret = pthread_barrier_wait(&pargs->barrier);
    if (ret != 0 && ret != PTHREAD_BARRIER_SERIAL_THREAD)
    {
        abort();
    }
    for (size_t i = 0; i < pargs->n_events; ++i)
    {
        MsgData md(buf, sizeof(buf));
        Message msg(&prod, &md);
        Message ack(&prod);
        prod.send(msg, &ack);
        test_assert(ack.get_val() == 0 || ack.get_val() == EAGAIN);
    }
    return 0;
}

START_TEST(test_net_consumer_nto1)
{
    log_info << "START";
    string url("tcp://localhost:2112?socket.non_blocking=1");
    NetConsumer cons(url);
    cons.connect(url);

    cons.start();

    pthread_t thds[8];

    producer_thd_args pargs(cons, 1000, 8);
    for (size_t i = 0; i < 8; ++i)
    {
        pthread_create(&thds[i], 0, &producer_thd, &pargs);
    }

    for (size_t i = 0; i < 8; ++i)
    {
        pthread_join(thds[i], 0);
    }

    log_debug << "stopping";
    cons.stop();
}
END_TEST


START_TEST(test_multicast)
{
    string maddr("[ff30::8000:1]");
    string ifaddr("[::]");

    // Check that MReq works

    Sockaddr sa1(resolve(URI("udp://" + ifaddr + ":0")).get_addr());
    MReq mreq(resolve(URI("udp://" + maddr + ":4567")).get_addr(), sa1);

    string mc1("udp://" + maddr + ":4567?socket.if_addr=" + ifaddr 
               + "&socket.if_loop=1");
    string mc2("udp://" + maddr + ":4567?socket.if_addr=" + ifaddr
               + "&socket.if_loop=1");

    Network net;
    Socket* s1(net.connect(mc1));
    Socket* s2(net.connect(mc2));

    byte_t msg[2] = {0, 1};
    Datagram dg(Buffer(msg, msg + 2));
    s1->send(&dg);

    net.wait_event(-1);
    net.wait_event(1 * gu::datetime::Sec);
    const Datagram* rdg1(s1->recv());
    const Datagram* rdg2(s2->recv());
    fail_if(rdg1 == 0 || rdg2 == 0);
    fail_if(rdg1->get_len() != 2 || rdg2->get_len() != 2);
    delete s1;
    delete s2;
}

END_TEST


START_TEST(trac_288)
{
    try
    {
        string url("tcp://do-not-resolve:0");
        (void)resolve(url);
    }
    catch (Exception& e)
    {
        log_debug << "exception was " << e.what();
    }
}
END_TEST


static bool enable_test_multicast(false);

Suite* gu_net_suite()
{
    Suite* s = suite_create("galerautils++ Networking");
    TCase* tc;

    if (enable_test_multicast == false)
    {
        tc = tcase_create("test_debug_logger");
        tcase_add_checked_fixture(tc, 
                                  &debug_logger_checked_setup,
                                  &debug_logger_checked_teardown);
        tcase_add_test(tc, test_debug_logger);
        suite_add_tcase(s, tc);

        tc = tcase_create("test_buffer");
        tcase_add_test(tc, test_buffer);
        suite_add_tcase(s, tc);


        tc = tcase_create("test_datagram");
        tcase_add_test(tc, test_datagram);
        suite_add_tcase(s, tc);


        tc = tcase_create("test_resolver");
        tcase_add_test(tc, test_resolver);
        suite_add_tcase(s, tc);

        tc = tcase_create("test_network_listen");
        tcase_add_test(tc, test_network_listen);
        suite_add_tcase(s, tc);

        tc = tcase_create("test_network_connect");
        tcase_add_test(tc, test_network_connect);
        suite_add_tcase(s, tc);

        tc = tcase_create("test_network_send");
        tcase_add_test(tc, test_network_send);
        tcase_set_timeout(tc, 10);
        suite_add_tcase(s, tc);

        tc = tcase_create("test_network_interrupt");
        tcase_add_test(tc, test_network_interrupt);
        suite_add_tcase(s, tc);

        tc = tcase_create("test_network_nonblocking");
        tcase_add_test(tc, test_network_nonblocking);
        tcase_set_timeout(tc, 10);
        suite_add_tcase(s, tc);

        tc = tcase_create("test_net_consumer");
        tcase_add_test(tc, test_net_consumer);
        tcase_set_timeout(tc, 10);
        suite_add_tcase(s, tc);

        tc = tcase_create("test_net_consumer_nto1");
        tcase_add_test(tc, test_net_consumer_nto1);
        tcase_set_timeout(tc, 10);
        suite_add_tcase(s, tc);
    }
    if (enable_test_multicast == true)
    {
        tc = tcase_create("test_multicast");
        tcase_add_test(tc, test_multicast);
        suite_add_tcase(s, tc);
    }

    tc = tcase_create("trac_288");
    tcase_add_test(tc, trac_288);
#if 0 /* bogus test, commenting out for now */
    suite_add_tcase(s, tc);
#endif

    return s;
}

