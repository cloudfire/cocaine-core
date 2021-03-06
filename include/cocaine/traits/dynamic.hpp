/*
    Copyright (c) 2013 Andrey Goryachev <andrey.goryachev@gmail.com>
    Copyright (c) 2011-2013 Other contributors as noted in the AUTHORS file.

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

#ifndef COCAINE_DYNAMIC_SERIALIZATION_TRAITS_HPP
#define COCAINE_DYNAMIC_SERIALIZATION_TRAITS_HPP

#include <cocaine/traits.hpp>

#include <cocaine/dynamic/dynamic.hpp>

namespace cocaine { namespace io {

template<>
struct type_traits<dynamic_t> {
    template<class Stream>
    struct pack_visitor:
        public boost::static_visitor<>
    {
        pack_visitor(msgpack::packer<Stream>& packer):
            m_packer(packer)
        { }

        void
        operator()(const dynamic_t::null_t&) const {
            m_packer << msgpack::type::nil();
        }

        void
        operator()(const dynamic_t::bool_t& v) const {
            m_packer << v;
        }

        void
        operator()(const dynamic_t::int_t& v) const {
            m_packer << v;
        }

        void
        operator()(const dynamic_t::uint_t& v) const {
            m_packer << v;
        }

        void
        operator()(const dynamic_t::double_t& v) const {
            m_packer << v;
        }

        void
        operator()(const dynamic_t::string_t& v) const {
            m_packer << v;
        }

        void
        operator()(const dynamic_t::array_t& v) const {
            m_packer.pack_array(v.size());

            for(size_t i = 0; i < v.size(); ++i) {
                v[i].apply(*this);
            }
        }

        void
        operator()(const dynamic_t::object_t& v) const {
            m_packer.pack_map(v.size());

            for(auto it = v.begin(); it != v.end(); ++it) {
                m_packer << it->first;
                it->second.apply(*this);
            }
        }

    private:
        msgpack::packer<Stream>& m_packer;
    };

    template<class Stream>
    static inline
    void
    pack(msgpack::packer<Stream>& packer, const dynamic_t& source) {
        source.apply(pack_visitor<Stream>(packer));
    }

    static inline
    void
    unpack(const msgpack::object& object, dynamic_t& target) {
        switch(object.type) {
        case msgpack::type::MAP: {
            dynamic_t::object_t container;

            msgpack::object_kv *ptr = object.via.map.ptr,
                               *const end = ptr + object.via.map.size;

            for(; ptr < end; ++ptr) {
                if(ptr->key.type != msgpack::type::RAW) {
                    // NOTE: The keys should be strings.
                    throw msgpack::type_error();
                }

                unpack(ptr->val, container[ptr->key.as<std::string>()]);
            }

            target = std::move(container);
        } break;

        case msgpack::type::ARRAY: {
            dynamic_t::array_t container;
            container.reserve(object.via.array.size);

            msgpack::object *ptr = object.via.array.ptr,
                            *const end = ptr + object.via.array.size;

            for(unsigned int index = 0; ptr < end; ++ptr, ++index) {
                container.push_back(dynamic_t());
                unpack(*ptr, container.back());
            }

            target = std::move(container);
        } break;

        case msgpack::type::RAW: {
            target = object.as<std::string>();
        } break;

        case msgpack::type::DOUBLE: {
            target = object.as<double>();
        } break;

        case msgpack::type::POSITIVE_INTEGER: {
            target = object.as<dynamic_t::uint_t>();
        } break;

        case msgpack::type::NEGATIVE_INTEGER: {
            target = object.as<dynamic_t::int_t>();
        } break;

        case msgpack::type::BOOLEAN: {
            target = object.as<bool>();
        } break;

        case msgpack::type::NIL: {
            target = dynamic_t::null_t();
        }}
    }
};

}} // namespace cocaine::io

#endif // COCAINE_DYNAMIC_TYPE_TRAITS_HPP
