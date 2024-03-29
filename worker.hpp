#pragma once
#ifndef WORKER_HPP__
#define WORKER_HPP__

#include "basic.hpp"

#include <boost/signals2.hpp>

namespace df
{

class worker : public std::enable_shared_from_this<worker>
{
    net::io_context::strand write_strand_;
    tcp::socket socket_;
    bool valid_ = true;
    using on_worker_response = boost::signals2::signal<void (pack::packet_pointer)>;
    on_worker_response on_worker_response_;
    on_worker_response on_worker_ack_;

public:
    template<typename Launcher>
    worker(net::io_context& io, tcp::socket socket, Launcher& l):
        write_strand_{io},
        socket_{std::move(socket)}
        {
            on_worker_response_.connect([&l] (pack::packet_pointer p) { l.on_worker_response(p); } );
            on_worker_ack_.     connect([&l] (pack::packet_pointer p) { l.on_worker_ack(p); } );
        }

    bool is_valid() { return valid_; }

    void start_read_header()
    {
        BOOST_LOG_TRIVIAL(trace) << "worker start_read_header";
        auto read_buf = std::make_shared<std::array<pack::unit_t, pack::packet_header::bytesize>>();
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf] (boost::system::error_code ec, std::size_t /*length*/) {
                if (not ec)
                {
                    pack::packet_pointer pack = std::make_shared<pack::packet>();
                    pack->header.parse(read_buf->data());

                    switch (pack->header.type)
                    {
                    case pack::msg_t::worker_dereg:
                        BOOST_LOG_TRIVIAL(debug) << "worker get worker_dereg" << pack->header;
                        self->valid_ = false;
                        self->start_read_header();
                        break;

                    case pack::msg_t::worker_response:
                        BOOST_LOG_TRIVIAL(debug) << "worker get resp " << pack->header;
                        self->start_read_body(pack);
                        break;

                    case pack::msg_t::ack:
                        BOOST_LOG_TRIVIAL(debug) << "worker get ack " << pack->header;
                        self->on_worker_ack_(pack);
                        self->start_read_header();
                        break;

                    case pack::msg_t::err:
                    case pack::msg_t::put:
                    case pack::msg_t::get:
                    case pack::msg_t::worker_reg:
                    case pack::msg_t::worker_push_request:
                    case pack::msg_t::trigger:
                    {
                        BOOST_LOG_TRIVIAL(error) << "worker packet error" << pack->header;
                        self->start_read_header();
                        break;
                    }
                    }
                }
                else
                {
                    if (ec != boost::asio::error::eof)
                        BOOST_LOG_TRIVIAL(error) << "worker start_read_header err: " << ec.message();
                    self->valid_ = false;
                }
            });
    }

    void start_read_body(pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "worker start_read_body";
        auto read_buf = std::make_shared<std::vector<pack::unit_t>>(pack->header.datasize);
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf, pack] (boost::system::error_code ec, std::size_t length) {
                if (not ec)
                {
                    pack->data.parse(length, read_buf->data());
                    BOOST_LOG_TRIVIAL(trace) << "worker start self->registered_job_";
                    self->on_worker_response_(pack);
                    self->start_read_header();
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "worker start_read_body: " << ec.message();
            });
    }

    //template<typename OnCompletion>
    void start_post(pack::packet_pointer pack)//, OnCompletion && oncomp)
    {
        BOOST_LOG_TRIVIAL(trace) << "worker start post";
        //registered_job_[pack->header].connect(std::forward<OnCompletion>(oncomp));
        start_write(pack);
    }

    void start_write(pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "worker start_write";
        auto buf_pointer = pack->serialize();
        net::async_write(
            socket_,
            net::buffer(buf_pointer->data(), buf_pointer->size()),
            net::bind_executor(
                write_strand_,
                [self=shared_from_this(), buf_pointer] (boost::system::error_code ec, std::size_t /*length*/) {
                    if (not ec)
                        BOOST_LOG_TRIVIAL(debug) << "worker wrote msg";
                    else
                        BOOST_LOG_TRIVIAL(error) << "worker start write error: " << ec.message();
                }));
    }
};

} // namespace df

#endif // WORKER_HPP__
