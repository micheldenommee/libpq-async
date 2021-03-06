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

#ifndef _libpq_async_data_prepared_h
#define _libpq_async_data_prepared_h

#include "data_common.h"
#include "log.h"

#include "data_connection_pool.h"
#include "data_reader.h"
#include "database.h"

#include "utils.h"

namespace pq_async {

#define _PQ_ASYNC_SEND_QRY_PREP_BODY_PARAMS(__val, __process_fn, __def_val) \
    parameters_t p; \
    p.push_back<sizeof...(PARAMS) -1>(args...); \
     \
    md::callback::value_cb<__val> cb; \
    md::callback::assign_value_cb<md::callback::value_cb<__val>, __val>(cb, get_last(args...)); \
     \
    this->_db->open_connection( \
    [self=this->shared_from_this(), \
        _p = std::move(p), \
        cb] \
    (const md::callback::cb_error& err, connection_lock lock){ \
        if(err){ \
            cb(err, __def_val); \
            return; \
        } \
         \
        try{ \
            auto ct = std::make_shared<connection_task_t>( \
                self->_db->_strand.get(), self->_db, lock, \
            [self, cb]( \
                const md::callback::cb_error& err, PGresult* r \
            )-> void { \
                if(err){ \
                    cb(err, __def_val); \
                    return; \
                } \
                 \
                try{ \
                    cb(nullptr, self->_db->__process_fn(r)); \
                }catch(const std::exception& err){ \
                    cb(md::callback::cb_error(err), __def_val); \
                } \
            }); \
            ct->send_query_prepared(self->_name.c_str(), _p); \
            self->_db->_strand->push_back(ct); \
             \
        }catch(const std::exception& err){ \
            cb(md::callback::cb_error(err), __def_val); \
        } \
    });

#define _PQ_ASYNC_SEND_QRY_PREP_BODY_T(__val, __process_fn, __def_val) \
    md::callback::value_cb<__val> cb; \
    md::callback::assign_value_cb<md::callback::value_cb<__val>, __val>(cb, acb); \
    this->_db->open_connection( \
    [self=this->shared_from_this(), \
        _p = std::move(p), \
        cb] \
    (const md::callback::cb_error& err, connection_lock lock){ \
        if(err){ \
            cb(err, __def_val); \
            return; \
        } \
         \
        try{ \
            auto ct = std::make_shared<connection_task_t>( \
                self->_db->_strand.get(), self->_db, lock, \
            [self, cb]( \
                const md::callback::cb_error& err, PGresult* r \
            )-> void { \
                if(err){ \
                    cb(err, __def_val); \
                    return; \
                } \
                 \
                try{ \
                    cb(nullptr, self->_db->__process_fn(r)); \
                }catch(const std::exception& err){ \
                    cb(md::callback::cb_error(err), __def_val); \
                } \
            }); \
            ct->send_query_prepared(self->_name.c_str(), _p); \
            self->_db->_strand->push_back(ct); \
             \
        }catch(const std::exception& err){ \
            cb(md::callback::cb_error(err), __def_val); \
        } \
    });

#define _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(__process_fn) \
    this->_db->wait_for_sync(); \
    auto lock = this->_db->open_connection(); \
    connection_task_t ct( \
        this->_db->_strand.get(), this->_db, lock \
    ); \
    ct.send_query_prepared(_name.c_str(), p); \
    return this->_db->__process_fn(ct.run_now());




class data_prepared_t
    : public std::enable_shared_from_this<data_prepared_t>
{
    friend database_t;
    
    data_prepared_t(
        database db, const std::string& name, bool auto_deallocate,
        connection_lock lock)
        : _db(db), _name(name), _auto_deallocate(auto_deallocate), _lock(lock)
    {
    }
    
public:
    
    ~data_prepared_t()
    {
        if(_auto_deallocate)
            _db->deallocate_prepared(this->_name.c_str());
    }
    
    database db(){ return _db;}
    
    
    
    /*!
     * \brief asynchrounously process a query
     * and returns the number of rows affected by insert, update and delete
     * 
     * \tparam PARAMS 
     * \tparam PQ_ASYNC_VALID_DB_CALLBACK(int) 
     * \param args query parameters, the last parameter is the query callback
     * pq_async::value_cb<int>
     */
    template<typename... PARAMS, PQ_ASYNC_VALID_DB_CALLBACK(int)>
    void execute(const PARAMS&... args)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_PARAMS(
            int, _process_execute_result, -1
        );
    }

    /*!
     * \brief asynchrounously process a query
     * and returns the number of rows affected by insert, update and delete
     * 
     * \tparam T 
     * \tparam PQ_ASYNC_VALID_DB_VAL_CALLBACK(T, int) 
     * \param p query parameters
     * \param acb completion void(const md::callback::cb_error&, int) callback
     */
    template<typename T, PQ_ASYNC_VALID_DB_VAL_CALLBACK(T, int)>
    void execute(const parameters_t& p, const T& acb)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_T(
            int, _process_execute_result, -1
        );
    }
    
    /*!
     * \brief synchrounously process a query
     * and returns the number of rows affected by insert, update and delete
     * 
     * \tparam PARAMS 
     * \tparam PQ_ASYNC_INVALID_DB_CALLBACK(int) 
     * \param args query parameters
     * \return int32_t the number of record processed
     */
    template<typename... PARAMS, PQ_ASYNC_INVALID_DB_CALLBACK(int)>
    int32_t execute(const PARAMS&... args)
    {
        parameters_t p(args...);
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_execute_result);
    }
    /*!
     * \brief synchrounously process a query
     * and returns the number of rows affected by insert, update and delete
     * 
     * \return int32_t the number of record processed
     */
    int32_t execute()
    {
        parameters_t p;
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_execute_result);
    }
    /*!
     * \brief synchrounously process a query
     * and returns the number of rows affected by insert, update and delete
     * 
     * \param p query parameters
     * \return int32_t the number of record processed
     */
    int32_t execute(const parameters_t& p)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_execute_result);
    }

    
    /*!
     * \brief asynchrounously process a query
     * and returns a pq_async::data_table_t as the result
     * 
     * \tparam PARAMS 
     * \tparam PQ_ASYNC_VALID_DB_CALLBACK(data_table) 
     * \param args query parameters, the last parameter is 
     * the completion void(const md::callback::cb_error&, data_table) callback
     */
    template<typename... PARAMS, PQ_ASYNC_VALID_DB_CALLBACK(data_table)>
    void query(const PARAMS&... args)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_PARAMS(
            data_table, _process_query_result, data_table()
        );
    }
    /*!
     * \brief asynchrounously process a query
     * and returns a pq_async::data_table_t as the result
     * 
     * \tparam T 
     * \tparam PQ_ASYNC_VALID_DB_VAL_CALLBACK(T, data_table) 
     * \param p query parameters
     * \param acb completion void(const md::callback::cb_error&, data_table) callback
     */
    template<typename T, PQ_ASYNC_VALID_DB_VAL_CALLBACK(T, data_table)>
    void query(const parameters_t& p, const T& acb)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_T(
            data_table, _process_query_result, data_table()
        );
    }
    
    /*!
     * \brief synchrounously process a query
     * and returns a pq_async::data_table_t as the result
     * 
     * \tparam PARAMS 
     * \tparam PQ_ASYNC_INVALID_DB_CALLBACK(data_table) 
     * \param args query parameters
     * \return data_table 
     */
    template<typename... PARAMS, PQ_ASYNC_INVALID_DB_CALLBACK(data_table)>
    data_table query(const PARAMS&... args)
    {
        parameters_t p(args...);
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_query_result);
    }
    /*!
     * \brief synchrounously process a query
     * and returns a pq_async::data_table_t as the result
     * 
     * \return data_table 
     */
    data_table query()
    {
        parameters_t p;
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_query_result);
    }
    /*!
     * \brief synchrounously process a query
     * and returns a pq_async::data_table_t as the result
     * 
     * \param p query parameters
     * \return data_table 
     */
    data_table query(const parameters_t& p)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_query_result);
    }
    
    /*!
     * \brief asynchrounously process a query
     * and returns a pq_async::data_row_t as the result
     * 
     * \tparam PARAMS 
     * \tparam PQ_ASYNC_VALID_DB_CALLBACK(data_row) 
     * \param args query parameters, the last parameter is
     * the completion void(const md::callback::cb_error&, data_row) callback
     */
    template<typename... PARAMS, PQ_ASYNC_VALID_DB_CALLBACK(data_row)>
    void query_single(const PARAMS&... args)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_PARAMS(
            data_row, _process_query_single_result, data_row()
        );
    }
    /*!
     * \brief asynchrounously process a query
     * and returns a pq_async::data_row_t as the result
     * 
     * \tparam T 
     * \tparam PQ_ASYNC_VALID_DB_VAL_CALLBACK(T, data_row) 
     * \param p query parameters
     * \param acb the completion void(const md::callback::cb_error&, data_row) callback
     */
    template<typename T, PQ_ASYNC_VALID_DB_VAL_CALLBACK(T, data_row)>
    void query_single(const parameters_t& p, const T& acb)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_T(
            data_row, _process_query_single_result, data_row()
        );
    }
    
    
    /*!
     * \brief synchrounously process a query
     * and returns a pq_async::data_row_t as the result
     * 
     * \tparam PARAMS 
     * \tparam PQ_ASYNC_INVALID_DB_CALLBACK(data_row) 
     * \param args query parameters
     * \return data_row 
     */
    template<typename... PARAMS, PQ_ASYNC_INVALID_DB_CALLBACK(data_row)>
    data_row query_single(const PARAMS&... args)
    {
        parameters_t p(args...);
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_query_single_result);
    }
    /*!
     * \brief synchrounously process a query
     * and returns a pq_async::data_row_t as the result
     * 
     * \return data_row 
     */
    data_row query_single()
    {
        parameters_t p;
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_query_single_result);
    }
    /*!
     * \brief synchrounously process a query
     * and returns a pq_async::data_row_t as the result
     * 
     * \param p query parameters
     * \return data_row 
     */
    data_row query_single(const parameters_t& p)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_query_single_result);
    }
    
    /*!
     * \brief asynchrounously process a query
     * and returns a scalar value as the result
     * 
     * \tparam R the result type
     * \tparam PARAMS 
     * \tparam PQ_ASYNC_VALID_DB_CALLBACK(R) 
     * \param args query parameters, the last parameter is the query callback
     * pq_async::value_cb<R>
     */
    template<typename R, typename... PARAMS, PQ_ASYNC_VALID_DB_CALLBACK(R)>
    void query_value(const PARAMS&... args)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_PARAMS(
            R, _process_query_value_result<R>, R()
        );
    }
    /*!
     * \brief asynchrounously process a query
     * and returns a scalar value as the result
     * 
     * \tparam R 
     * \tparam T 
     * \tparam PQ_ASYNC_VALID_DB_VAL_CALLBACK(T, R) 
     * \param p query parameters
     * \param acb completion void(const md::callback::cb_error&, R) callback
     */
    template<
        typename R, typename T,
        PQ_ASYNC_VALID_DB_VAL_CALLBACK(T, R)
    >
    void query_value(const parameters_t& p, const T& acb)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_T(
            R, _process_query_value_result<R>, R()
        );
    }
    
    /*!
     * \brief synchrounously process a query
     * and returns a scalar value as the result
     * 
     * \tparam R the result type
     * \tparam PARAMS 
     * \tparam PQ_ASYNC_INVALID_DB_CALLBACK(R) 
     * \param args query parameters
     * \return R the scalar value
     */
    template<typename R, typename... PARAMS, PQ_ASYNC_INVALID_DB_CALLBACK(R)>
    R query_value(const PARAMS&... args)
    {
        parameters_t p(args...);
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_query_value_result<R>);
    }
    /*!
     * \brief synchrounously process a query
     * and returns a scalar value as the result
     * 
     * \tparam R 
     * \return R 
     */
    template<typename R>
    R query_value()
    {
        parameters_t p;
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_query_value_result<R>);
    }
    /*!
     * \brief synchrounously process a query
     * and returns a scalar value as the result
     * 
     * \tparam R 
     * \param p query parameters
     * \return R 
     */
    template<typename R>
    R query_value(const parameters_t& p)
    {
        _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC(_process_query_value_result<R>);
    }
    
    
    
    /*!
     * \brief asynchrounously process a query
     * and returns a pq_async::data_reader_t as the result
     * 
     * \tparam PARAMS 
     * \tparam PQ_ASYNC_VALID_DB_CALLBACK(data_reader) 
     * \param args query parameters, the last parameter is the query callback
     * pq_async::value_cb<data_reader>
     */
    template<typename... PARAMS, PQ_ASYNC_VALID_DB_CALLBACK(data_reader)>
    void query_reader(const PARAMS&... args)
    {
        parameters_t p;
        p.push_back<sizeof...(PARAMS) -1>(args...);
        
        md::callback::value_cb<data_reader> cb;
        md::callback::assign_value_cb<
            md::callback::value_cb<data_reader>,
            data_reader
        >(
            cb, get_last(args...)
        );
        
        this->_db->open_connection(
        [self=this->shared_from_this(),
            _p = std::move(p),
            cb]
        (const md::callback::cb_error& err, connection_lock lock){
            if(err){
                cb(err, data_reader());
                return;
            }
            
            try{
                auto ct = std::make_shared<reader_connection_task>(
                    self->_db->_strand.get(), self->_db, lock
                );
                cb(nullptr, std::shared_ptr<data_reader_t>(new data_reader_t(ct)));
                ct->send_query_prepared(self->_name.c_str(), _p);
                self->_db->_strand->push_back(ct);
                
            }catch(const std::exception& err){
                cb(md::callback::cb_error(err), data_reader());
            }
        });
    }
    /*!
     * \brief asynchrounously process a query
     * and returns a pq_async::data_reader_t as the result
     * 
     * \tparam T 
     * \tparam PQ_ASYNC_VALID_DB_VAL_CALLBACK(T, data_reader) 
     * \param p query parameters
     * \param acb completion void(const md::callback::cb_error&, data_reader) callback
     */
    template<typename T, PQ_ASYNC_VALID_DB_VAL_CALLBACK(T, data_reader)>
    void query_reader(const parameters_t& p, const T& acb)
    {
        md::callback::value_cb<data_reader> cb;
        md::callback::assign_value_cb<
            md::callback::value_cb<data_reader>,
            data_reader
        >(cb, acb);

        this->_db->open_connection(
        [self=this->shared_from_this(),
            _p = std::move(p),
            cb]
        (const md::callback::cb_error& err, connection_lock lock){
            if(err){
                cb(err, data_reader());
                return;
            }
            
            try{
                auto ct = std::make_shared<reader_connection_task>(
                    self->_db->_strand.get(), self->_db, lock
                );
                cb(nullptr, std::shared_ptr<data_reader_t>(new data_reader_t(ct)));
                ct->send_query_prepared(self->_name.c_str(), _p);
                self->_db->_strand->push_back(ct);
                
            }catch(const std::exception& err){
                cb(md::callback::cb_error(err), data_reader());
            }
        });
    }
    
    
    /*!
     * \brief synchrounously process a query
     * and returns a pq_async::data_reader_t as the result
     * 
     * \tparam PARAMS 
     * \tparam PQ_ASYNC_INVALID_DB_CALLBACK(data_reader) 
     * \param args query parameters
     * \return data_reader 
     */
    template<typename... PARAMS, PQ_ASYNC_INVALID_DB_CALLBACK(data_reader)>
    data_reader query_reader(const PARAMS&... args)
    {
        this->_db->wait_for_sync();
        auto lock = this->_db->open_connection();
        parameters_t p(args...);
        auto ct = std::make_shared<reader_connection_task>(
            this->_db->_strand.get(), this->_db, lock
        );
        ct->send_query_prepared(_name.c_str(), p);
        return std::shared_ptr<data_reader_t>(new data_reader_t(ct));
    }
    /*!
     * \brief synchrounously process a query
     * and returns a pq_async::data_reader_t as the result
     * 
     * \return data_reader 
     */
    data_reader query_reader()
    {
        this->_db->wait_for_sync();
        auto lock = this->_db->open_connection();
        parameters_t p;
        auto ct = std::make_shared<reader_connection_task>(
            this->_db->_strand.get(), this->_db, lock
        );
        ct->send_query_prepared(_name.c_str(), p);
        return std::shared_ptr<data_reader_t>(new data_reader_t(ct));
    }
    /*!
     * \brief synchrounously process a query
     * and returns a pq_async::data_reader_t as the result
     * 
     * \param p query parameters
     * \return data_reader 
     */
    data_reader query_reader(const parameters_t& p)
    {
        this->_db->wait_for_sync();
        auto lock = this->_db->open_connection();
        auto ct = std::make_shared<reader_connection_task>(
            this->_db->_strand.get(), this->_db, lock
        );
        ct->send_query_prepared(_name.c_str(), p);
        return std::shared_ptr<data_reader_t>(new data_reader_t(ct));
    }
    
    
private:
    database _db;
    std::string _name;
    bool _auto_deallocate;
    connection_lock _lock;
};


#undef _PQ_ASYNC_SEND_QRY_PREP_BODY_PARAMS
#undef _PQ_ASYNC_SEND_QRY_PREP_BODY_T
#undef _PQ_ASYNC_SEND_QRY_PREP_BODY_SYNC

} // ns: pq_async
#endif //_libpq_async_data_prepared_h
