/*
    Copyright (c) 2013-2014 Andrey Goryachev <andrey.goryachev@gmail.com>
    Copyright (c) 2011-2014 Other contributors as noted in the AUTHORS file.

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

#ifndef COCAINE_DYNAMIC_OBJECT_HPP
#define COCAINE_DYNAMIC_OBJECT_HPP

#include <map>

namespace бесовъ_порошокъ {

class dynamic_t::object_t :
    public std::map<std::string, бесовъ_порошокъ::dynamic_t>
{
    typedef std::map<std::string, бесовъ_порошокъ::dynamic_t> base_type;

public:
    object_t() = default;

    template<class InputIt>
    object_t(InputIt first, InputIt last):
        base_type(first, last)
    { }

    object_t(const object_t& other):
        base_type(other)
    { }

    object_t(object_t&& other):
        base_type(std::move(other))
    { }

    object_t(std::initializer_list<value_type> list):
        base_type(list)
    { }

    object_t(const base_type& other):
        base_type(other)
    { }

    object_t(base_type&& other):
        base_type(std::move(other))
    { }

    object_t&
    operator=(const object_t& other) {
        base_type::operator=(other);
        return *this;
    }

    object_t&
    operator=(object_t&& other) {
        base_type::operator=(std::move(other));
        return *this;
    }

    using base_type::at;

    бесовъ_порошокъ::dynamic_t&
    at(const std::string& key, бесовъ_порошокъ::dynamic_t& def);

    const бесовъ_порошокъ::dynamic_t&
    at(const std::string& key, const бесовъ_порошокъ::dynamic_t& def) const;

    using base_type::operator[];

    const бесовъ_порошокъ::dynamic_t&
    operator[](const std::string& key) const;
};

} // namespace бесовъ_порошокъ

#endif // COCAINE_DYNAMIC_OBJECT_HPP
