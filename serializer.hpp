#pragma once
#ifndef CPP_SERIALIZER_OBJECTPACK_HPP__
#define CPP_SERIALIZER_OBJECTPACK_HPP__

#include <arpa/inet.h>

#include <boost/functional/hash.hpp>

#include <array>
#include <tuple>

namespace pack
{

using unit_t = std::uint8_t;
using key_t = std::array<unit_t, 256 / 8 / sizeof(unit_t)>;
enum class msg_t: unit_t
{
    error,
    put,
    get,
    response,
    call_register,
};

template<typename Integer>
auto hton(Integer i) -> Integer
{
    if constexpr (sizeof(Integer) == sizeof(decltype(htonl(i))))
        return htonl(i);
    else if constexpr (sizeof(Integer) == sizeof(decltype(htons(i))))
        return htons(i);
    else if constexpr (sizeof(Integer) == 1)
        return i;
    else
    {
        static_assert("not supported conversion");
        return -1;
    }
}

template<typename Integer>
auto ntoh(Integer i) -> Integer
{
    if constexpr (sizeof(Integer) == sizeof(decltype(ntohl(i))))
        return ntohl(i);
    else if constexpr (sizeof(Integer) == sizeof(decltype(ntohs(i))))
        return ntohs(i);
    else if constexpr (sizeof(Integer) == 1)
        return i;
    else
    {
        static_assert("not supported conversion");
        return -1;
    }
}


struct packet_header
{
    msg_t type;
    key_t buf;
    std::uint32_t datasize;
    static constexpr int bytesize = std::tuple_size<key_t>::value + sizeof(datasize) + sizeof(type);

    void parse(unit_t *pos)
    {
        // |type|
        std::memcpy(std::addressof(type), pos, sizeof(type));
        pos += sizeof(type);

        // |key|
        std::memcpy(buf.data(), pos, buf.size());
        pos += buf.size();

        // |datasize|
        std::memcpy(std::addressof(datasize), pos, sizeof(datasize));
        datasize = ntoh(datasize);
    }

    auto dump(unit_t *pos) -> unit_t*
    {
        // |type|
        std::memcpy(pos, std::addressof(type), sizeof(type));
        pos += sizeof(type);

        // |key|
        std::memcpy(pos, buf.data(), buf.size());
        pos += buf.size();

        // |datasize|
        decltype(datasize) datasize_copy = hton(datasize);
        std::memcpy(pos, std::addressof(datasize_copy), sizeof(datasize_copy));
        return pos + sizeof(datasize_copy);
    }
};

struct packet_header_key_hash
{
    auto operator() (packet_header const& key) const -> std::size_t { return boost::hash_value(key.buf); }
};

struct packet_header_key_compare
{
    bool operator() (packet_header const& key1, packet_header const& key2) const { return ( key1.buf == key2.buf ); }
};

auto operator <<(std::ostream &os, packet_header const& pd) -> std::ostream&
{
    os << "[t=" << static_cast<int>(pd.type) << "|k=";
    for (key_t::value_type v: pd.buf)
        os << std::hex << static_cast<int>(v);
    os << "|d=" << pd.datasize << "]";
    return os;
}

struct packet_data
{
    std::vector<unit_t> buf;

    void parse(std::uint32_t const& size, unit_t *pos)
    {
        buf.resize(size);
        std::memcpy(buf.data(), pos, size);
    }

    auto dump(unit_t *pos) -> unit_t*
    {
        std::memcpy(pos, buf.data(), buf.size());
        return pos + buf.size();
    }
};


struct packet
{
    packet_header header;
    packet_data data;

    auto serialize() -> std::shared_ptr<std::vector<unit_t>>
    {
        header.datasize = data.buf.size();
        auto r = std::make_shared<std::vector<unit_t>>(packet_header::bytesize + header.datasize);

        BOOST_LOG_TRIVIAL(trace) << "updated header size: " << header.datasize;

        unit_t* pos = header.dump(r->data());
        data.dump(pos);

        return r;
    }
};

using packet_pointer = std::shared_ptr<packet>;

} // namespace pack

#endif // CPP_SERIALIZER_OBJECTPACK_HPP__
