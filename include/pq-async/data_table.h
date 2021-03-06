/*
MIT License

Copyright (c) 2011-2019 Michel Dénommée

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef _libpq_async_data_table_h
#define _libpq_async_data_table_h

#define LIBPQ_ASYNC_TBL_ADD_GETTER(type, name) \
    type as_##name(uint32_t row_idx, const char* col_name) const \
    { \
        return get_value(row_idx, col_name)->as_##name(); \
    } \
    type as_##name(uint32_t row_idx, int32_t col_idx) const \
    { \
        return get_value(row_idx, (uint32_t)col_idx)->as_##name(); \
    }


#include "data_common.h"
#include "data_row.h"

namespace pq_async{

class data_table_t
    : public std::vector<data_row>
{
    friend class data_reader_t;
public:
    data_table_t();

    virtual ~data_table_t();

    void dispose()
    {

    }

    data_columns_container get_columns() const
    {
        return _cols;
    }
    
    size_t col_count() const { return _cols->size();}
    
    int32_t get_col_index(const char* col_name) const
    {
        return _cols->get_col_index(col_name);
    }


    data_value get_value(uint32_t row_idx, uint32_t col_id) const;
    data_value get_value(uint32_t row_idx, const char* col_name) const;
    data_value get_value(uint32_t row_idx, const std::string& col_name) const
    {
        return get_value(row_idx, col_name.c_str());
    }

    /////  values
    bool is_null(uint32_t row_idx, const char* col_name) const
    {
        return get_value(row_idx, col_name)->is_null();
    }
    
    template < typename T >
    T as(uint32_t row_idx, const char* col_name) const
    {
        return get_value(row_idx, col_name)->as<T>();
    }
    
    LIBPQ_ASYNC_TBL_ADD_GETTER(bool, bool)
    LIBPQ_ASYNC_TBL_ADD_GETTER(std::string, text)
    LIBPQ_ASYNC_TBL_ADD_GETTER(int16_t, int16)
    LIBPQ_ASYNC_TBL_ADD_GETTER(int32_t, int32)
    LIBPQ_ASYNC_TBL_ADD_GETTER(int64_t, int64)
    
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::numeric, numeric)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::money, money)

    LIBPQ_ASYNC_TBL_ADD_GETTER(float, float)
    LIBPQ_ASYNC_TBL_ADD_GETTER(double, double)
    
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::time, time)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::time_tz, time_tz)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::timestamp, timestamp)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::timestamp_tz, timestamp_tz)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::date, date)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::interval, interval)
    
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::json, json)
    LIBPQ_ASYNC_TBL_ADD_GETTER(std::vector<int8_t>, bytea)
    
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::uuid, uuid)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::oid, oid)

    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::cidr, cidr)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::inet, inet)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::macaddr, macaddr)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::macaddr8, macaddr8)
    
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::point, point)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::line, line)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::lseg, lseg)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::box, box)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::path, path)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::polygon, polygon)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::circle, circle)
    
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::int4range, int4range)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::int8range, int8range)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::numrange, numrange)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::tsrange, tsrange)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::tstzrange, tstzrange)
    LIBPQ_ASYNC_TBL_ADD_GETTER(pq_async::daterange, daterange)
    
    
    bool is_array(uint32_t row_idx, const char* col_name) const
    {
        return get_value(row_idx, col_name)->is_array();
    }
    
    //////////////////////////////
    
    data_value operator()(uint32_t i, const char* col_name) const 
    {
        return get_value(i, col_name);
    }
    
    const data_row_t* get_row_ptr(uint32_t i) const
    {
        return (*this)[i].get();
    }

    data_row get_row_safe(uint32_t i) const
    {
        return (*this)[i];
    }
    
    
    void to_json(pq_async::json& rows) const;
    void to_json_string(
        std::string& str, const unsigned int current_indent = 0
    ) const;

private:
    data_columns_container _cols;
    //std::vector< data_row > _rows;

};

#undef LIBPQ_ASYNC_TBL_ADD_GETTER

} //namespace pq_async
#endif //_libpq_async_data_table_h
