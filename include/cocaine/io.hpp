/*
    Copyright (c) 2011-2012 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2012 Other contributors as noted in the AUTHORS file.

    This file is part of Cocaine.

    Cocaine is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Cocaine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>. 
*/

#ifndef COCAINE_IO_HPP
#define COCAINE_IO_HPP

#include "cocaine/common.hpp"
#include "cocaine/birth_control.hpp"
#include "cocaine/traits.hpp"

#include <iostream>

#include <fcntl.h>
#include <sys/un.h>

#include <zmq.hpp>

#if ZMQ_VERSION < 20200
    #error ZeroMQ version 2.2.0+ required!
#endif

namespace cocaine { namespace io {

// ZeroMQ socket

class socket_base_t: 
    public boost::noncopyable,
    public birth_control<socket_base_t>
{
    public:
        socket_base_t(context_t& context,
                      int type);

        virtual
        ~socket_base_t();

        void
        bind();

        void
        bind(const std::string& endpoint);
        
        void
        connect(const std::string& endpoint);

        bool
        send(zmq::message_t& message,
             int flags = 0);

        bool
        recv(zmq::message_t& message,
             int flags = 0);

        void
        drop();

        void
        getsockopt(int name,
                   void * value,
                   size_t * size);

        void
        setsockopt(int name,
                   const void * value,
                   size_t size);

    public:
        int
        fd() const {
            return m_fd;
        }

        std::string
        endpoint() const { 
            return m_endpoint; 
        }

        bool
        more() {
            int64_t rcvmore = 0;
            size_t size = sizeof(rcvmore);

            getsockopt(ZMQ_RCVMORE, &rcvmore, &size);

            return rcvmore != 0;
        }

        std::string
        identity() {
            char identity[256] = { 0 };
            size_t size = sizeof(identity);

            getsockopt(ZMQ_IDENTITY, &identity, &size);

            return identity;
        }

        bool
        pending(unsigned long event = ZMQ_POLLIN) {
            unsigned long events = 0;
            size_t size = sizeof(events);

            getsockopt(ZMQ_EVENTS, &events, &size);

            return events & event;
        }

    protected:
        zmq::socket_t m_socket;
        
    private:
        context_t& m_context;

        int m_fd;
        std::string m_endpoint;

        uint16_t m_port;
};

// Socket options

namespace options {
    struct receive_timeout {
        typedef int value_type;
        typedef boost::mpl::int_<ZMQ_RCVTIMEO> option_type;
    };

    struct send_timeout {
        typedef int value_type;
        typedef boost::mpl::int_<ZMQ_SNDTIMEO> option_type;
    };
}

template<class Option>
class scoped_option {
    typedef typename Option::value_type value_type;
    typedef typename Option::option_type option_type;

    public:
        scoped_option(socket_base_t& socket,
                      value_type value):
            m_socket(socket),
            m_saved(value_type()),
            m_size(sizeof(m_saved))
        {
            m_socket.getsockopt(option_type(), &m_saved, &m_size);
            m_socket.setsockopt(option_type(), &value, sizeof(value));
        }

        ~scoped_option() {
            m_socket.setsockopt(option_type(), &m_saved, m_size);
        }

    private:
        socket_base_t& m_socket;
        value_type m_saved;
        size_t m_size;
};

// Serializing socket

struct socket_t:
    public socket_base_t
{
    socket_t(context_t& context,
             int type):
        socket_base_t(context, type)
    { }

    template<class T>
    socket_t(context_t& context,
             int type,
             const T& identity):
        socket_base_t(context, type)
    {
        msgpack::sbuffer buffer;
        msgpack::packer<msgpack::sbuffer> packer(buffer);

        // NOTE: Allow any serializable type to be socket's identity,
        // for example UUIDs.
        type_traits<T>::pack(packer, identity);

        setsockopt(ZMQ_IDENTITY, buffer.data(), buffer.size());
    }

    using socket_base_t::send;
    using socket_base_t::recv;

    // Plain strings

    bool
    send(const std::string& blob,
         int flags = 0)
    {
        zmq::message_t message(blob.size());
        
        memcpy(
            message.data(),
            blob.data(),
            blob.size()
        );

        return send(message, flags);
    }

    // Serialized objects

    template<class T>
    bool
    send(const T& value,
         int flags = 0)
    {
        msgpack::sbuffer buffer;
        msgpack::packer<msgpack::sbuffer> packer(buffer);

        type_traits<T>::pack(packer, value);

        zmq::message_t message(buffer.size());

        std::memcpy(
            message.data(),
            buffer.data(),
            buffer.size()
        );
        
        return send(message, flags);
    }
    
    // Multipart messages

    template<class Head>
    bool
    send_multipart(Head&& head) {
        return send(head);
    }

    template<class Head, class... Tail>
    bool
    send_multipart(Head&& head,
                   Tail&&... tail)
    {
        return send(head, ZMQ_SNDMORE) &&
               send_multipart(std::forward<Tail>(tail)...);
    }

    // Plain strings

    bool
    recv(std::string& blob,
         int flags = 0)
    {
        zmq::message_t message;

        if(!recv(message, flags)) {
            return false;
        }

        blob.assign(
            static_cast<const char*>(message.data()),
            message.size()
        );

        return true;
    }

    // Serialized objects

    template<class T>
    bool
    recv(T& result,
         int flags = 0)
    {
        zmq::message_t message;
        msgpack::unpacked unpacked;

        if(!recv(message, flags)) {
            return false;
        }
       
        try { 
            msgpack::unpack(
                &unpacked,
                static_cast<const char*>(message.data()),
                message.size()
            );
        } catch(const msgpack::unpack_error& e) {
            throw cocaine::error_t("corrupted object");
        }
           
        try {
            type_traits<T>::unpack(unpacked.get(), result);
        } catch(const msgpack::type_error& e) {
            throw cocaine::error_t("corrupted object");
        } catch(const std::bad_cast& e) {
            throw cocaine::error_t("corrupted object - type mismatch");
        }

        return true;
    }

    // Multipart messages

    template<class Head>
    bool
    recv_multipart(Head&& head) {
        return recv(head);
    }

    template<class Head, class... Tail>
    bool
    recv_multipart(Head&& head,
                   Tail&&... tail)
    {
        return recv(head) &&
               recv_multipart(std::forward<Tail>(tail)...);
    }
};

// New stuff

struct io_error_t:
    public cocaine::error_t
{
    template<typename... Args>
    io_error_t(const std::string& format, Args&&... args):
        cocaine::error_t(format, std::forward<Args>(args)...)
    {
#ifdef _GNU_SOURCE
        m_message = ::strerror_r(errno, m_buffer, 256);
#else
        ::strerror_r(errno, m_buffer, 256);

        // NOTE: XSI-compliant strerror_r() returns int instead of the
        // string buffer, so complete the job manually.
        m_message = m_buffer;
#endif
    }

    virtual
    const char *
    what() const throw() {
        return m_message;
    }

private:
    char m_buffer[256];
    const char * m_message;
};

struct pipe_t:
    boost::noncopyable
{
    pipe_t(const std::string& path):
        m_fd(::socket(AF_LOCAL, SOCK_STREAM, 0))
    {
        if(m_fd == -1) {
            throw io_error_t("unable to create a pipe");
        }
        
        // Set non-blocking and close-on-exec options.
        configure(m_fd);

        struct sockaddr_un address = { AF_LOCAL, { 0 } };

        ::memcpy(address.sun_path, path.c_str(), path.size());

        if(::connect(m_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
            throw io_error_t("unable to connect a pipe to '%s'", path);
        }
    }

    pipe_t(int fd):
        m_fd(fd)
    { }

    pipe_t(pipe_t&& other):
        m_fd(-1)
    {
        *this = std::move(other);
    }

    pipe_t&
    operator=(pipe_t&& other) {
        std::swap(m_fd, other.m_fd);
        return *this;
    }

    ~pipe_t() {
        if(m_fd != -1 && ::close(m_fd) != 0) {
            // Log.
        }
    }

    ssize_t
    write(const char * buffer, size_t size) {
        ssize_t length = ::write(m_fd, buffer, size);
        
        if(length == -1) {
            switch(errno) {
                case EAGAIN || EWOULDBLOCK:
                case EINTR:
                    return 0;

                default:
                    throw io_error_t("unable to write to a pipe");
            }
        }

        return length;
    }

    ssize_t
    write(const std::string& chunk) {
        return write(chunk.data(), chunk.size());
    }

    ssize_t
    read(char * buffer, size_t size) {
        ssize_t length = ::read(m_fd, buffer, size);
    
        if(length == -1) {
            switch(errno) {
                case EAGAIN || EWOULDBLOCK:
                case EINTR:
                    return 0;

                default:
                    throw io_error_t("unable to read from a pipe");
            }
        }

        return length;
    }

public:
    int
    fd() const {
        return m_fd;
    }

private:
    void
    configure(int fd) {
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        ::fcntl(fd, F_SETFL, O_NONBLOCK);
    }

private:
    int m_fd;
};

struct acceptor_t:
    boost::noncopyable
{
    acceptor_t(const std::string& path,
               int backlog = 128):
        m_fd(::socket(AF_LOCAL, SOCK_STREAM, 0))
    {
        if(m_fd == -1) {
            throw io_error_t("unable to create an acceptor");
        }

        // Set non-blocking and close-on-exec options.
        configure(m_fd);

        struct sockaddr_un address = { AF_LOCAL, { 0 } };

        ::memcpy(address.sun_path, path.c_str(), path.size());

        if(::bind(m_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
            throw io_error_t("unable to bind an acceptor on '%s'", path);
        }

        ::listen(m_fd, backlog);
    }

    acceptor_t(acceptor_t&& other):
        m_fd(-1)
    {
        *this = std::move(other);
    }

    acceptor_t&
    operator=(acceptor_t&& other) {
        std::swap(m_fd, other.m_fd);
        return *this;
    }

    ~acceptor_t() {
        if(m_fd == -1) {
            return;
        }

        struct sockaddr_un address = { AF_LOCAL, { 0 } };
        socklen_t length = sizeof(address);

        ::getsockname(m_fd, reinterpret_cast<sockaddr*>(&address), &length);

        if(::close(m_fd) != 0) {
            // Log.
        }

        if(::unlink(address.sun_path) != 0) {
            // Log.
        }
    }

    boost::shared_ptr<pipe_t>
    accept() {
        struct sockaddr_un address = { AF_LOCAL, { 0 } };
        socklen_t length = sizeof(address);

        int fd = ::accept(m_fd, reinterpret_cast<sockaddr*>(&address), &length);

        if(fd == -1) {
            switch(errno) {
                case EAGAIN || EWOULDBLOCK:
                case EINTR:
                    return boost::shared_ptr<pipe_t>();

                default:
                    throw io_error_t("unable to accept a connection");
            }
        }

        // Set non-blocking and close-on-exec options.
        configure(fd);
        
        return boost::make_shared<pipe_t>(fd);
    }

public:
    int
    fd() const {
        return m_fd;
    }

private:
    void
    configure(int fd) {
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        ::fcntl(fd, F_SETFL, O_NONBLOCK);
    }

private:
    int m_fd;
};

}} // namespace cocaine::io

#endif
