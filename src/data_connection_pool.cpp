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

#include "data_connection_pool.h"
#include "database.h"

namespace pq_async{

std::atomic<int> pq_async::connection::s_next_id(-1);

static void pqasync_notice_processor(void *arg, const char *message)
{
    /*
        DEBUG, LOG, INFO, NOTICE, WARNING, and EXCEPTION
    */
    std::string s(message);
    std::string d("DEBUG:");
    std::string l("LOG:");
    std::string i("INFO:");
    std::string n("NOTICE:");
    std::string w("WARNING:");
    std::string e("EXCEPTION:");
    
    if(s.compare(0, d.length(), d) == 0){
        pq_async::default_logger()->trace("{}", message);
    }else if(s.compare(0, l.length(), l) == 0){
        pq_async::default_logger()->debug("{}", message);
    }else if(s.compare(0, i.length(), i) == 0){
        pq_async::default_logger()->info("{}", message);
    }else if(s.compare(0, n.length(), n) == 0){
        pq_async::default_logger()->warn("{}", message);
    }else if(s.compare(0, w.length(), w) == 0){
        pq_async::default_logger()->warn("{}", message);
    }else if(s.compare(0, e.length(), e) == 0){
        pq_async::default_logger()->error("{}", message);
    }else{
        pq_async::default_logger()->warn("{}", message);
    }
}

void pq_async::connection::set_notice_processor()
{
    PQsetNoticeProcessor(_conn, &pqasync_notice_processor, NULL);
}

bool pq_async::connection::lock()
{
    if(_res.load() == 0){
        _res.store(1);
        stop_work();
        PQ_ASYNC_DEF_TRACE(
            "connection '{}' lock acquired", this->id()
        );
        return true;
    } else
        return false;
}

void pq_async::connection::release()
{
    this->_owner = NULL;
    if(is_in_transaction.load()){
        
        PGresult* res = PQexec(_conn, "ROLLBACK");
        if(PQresultStatus(res) != PGRES_COMMAND_OK){
            std::string errMsg = PQerrorMessage(_conn);
            PQclear(res);
            throw pq_async::exception(errMsg);
        }

        is_in_transaction.store(false);
        PQclear(res);
    }
    
    if(_res.load() > 0){
        _res.store(0);
        PQ_ASYNC_DEF_TRACE(
            "connection '{}' lock released", this->id()
        );
    }
    
    stop_work();
    
    return;
}

md::event_strand<int> connection::strand()
{
    if(!this->_owner)
        return md::event_strand<int>();
    
    return this->_owner->get_strand();
}

bool pq_async::connection::is_dead()
{
    if(is_in_transaction.load() || _res.load() > 0)
        return false;
    
    std::chrono::system_clock::time_point
        timeout_date(
            std::chrono::system_clock::now() -
            std::chrono::seconds(-15)
        );
    return _last_modification_date < timeout_date;
}


connection_task_t::connection_task_t(
    md::event_queue_t* owner, database db, connection_lock lock,
    const md::callback::value_cb<PGresult*>& cb)
    : event_task_base_t(owner), 
    _cmd_type(command_type::none),
    _completed(false), _db(db),
    
    _conn(nullptr), _lock_cb(),
    _lock(lock), _cb(cb),

    _ev(nullptr)
{
}

connection_task_t::connection_task_t(
    md::event_queue_t* owner, database db, connection* conn,
    const md::callback::value_cb<connection_lock>& lock_cb)
    : event_task_base_t(owner), 
    _cmd_type(command_type::none),
    _completed(false), _db(db),
    
    _conn(conn), _lock_cb(lock_cb),
    _lock(), _cb(),
    
    _ev(nullptr)
{
}

connection_task_t::connection_task_t(
    md::event_queue_t* owner, database db, connection_lock lock)
    : event_task_base_t(owner), 
    _cmd_type(command_type::none),
    _completed(false), _db(db),
    
    _conn(nullptr), _lock_cb(),
    _lock(lock), _cb(),

    _ev(nullptr)
{
}

connection_task_t::connection_task_t(
    md::event_queue_t* owner, database db, connection* conn)
    : event_task_base_t(owner), 
    _cmd_type(command_type::none),
    _completed(false), _db(db),
    
    _conn(conn), _lock_cb(),
    _lock(), _cb(),

    _ev(nullptr)
{
}

void pq_async::connection_task_t::_connect()
{
    PQ_ASYNC_DBG(
        _db->_log,
        "assigning a connection\nct: {:p}, db: {:p}",
        (void*)this, (void*)_db.get()
    );
    
    if(_format < std::chrono::system_clock::now().time_since_epoch().count()){
        _completed = true;
        _lock_cb(
            md::callback::cb_error(
            pq_async::exception(
                "Connection request has time out!"
            )), connection_lock()
        );
        return;
    }
    
    try{
        #ifdef PQ_ASYNC_THREAD_SAFE
        std::unique_lock<std::recursive_mutex> lock(
            connection_pool::instance()->conn_pool_mutex
        );
        #endif
        
        if(_db->_conn == NULL)
            _db->_conn = connection_pool::get_connection(
                _db.get(), _sql, 1
            );
        
        _db->_conn->reserve();
        if(_db->_conn == NULL)
            _db->_conn = connection_pool::get_connection(
                _db.get(), _sql, 1
            );
        
        _db->_conn->open_connection();
        
        #ifdef PQ_ASYNC_THREAD_SAFE
        lock.unlock();
        connection_pool::notify_all();
        #endif
        
        connection_lock cl(new connection_lock_t(_db->_conn));
        _completed = true;
        _lock_cb(nullptr, cl);
    
    }catch(const pq_async::connection_pool_assign_exception& /*err*/){
        return;
    }catch(const std::exception& err){
        _completed = true;
        _lock_cb(md::callback::cb_error(err), connection_lock());
    }
}

reader_connection_task::reader_connection_task(
    md::event_queue_t* owner, database db, connection_lock lock)
    : connection_task_t(owner, db, lock)
{
}


PGconn* connection_task_t::conn(){ return _db->_conn->conn();}

std::string pq_async::connection_pool::last_stolen_conn_id = "";
pq_async::connection_pool *pq_async::connection_pool::s_instance = NULL;
bool pq_async::connection_pool::s_init = false;


void pq_async::connection_pool::init(bool init_ssl, bool init_crypto)
{
    if(s_init)
        return;
    
    PQinitOpenSSL(init_ssl ? 1 : 0, init_crypto ? 1 : 0);
    
    s_instance = new pq_async::connection_pool();
    s_init = true;
}

void pq_async::connection_pool::init(
    int max_pool_conn_count, bool init_ssl, bool init_crypto)
{
    if(s_init)
        return;

    PQinitOpenSSL(init_ssl ? 1 : 0, init_crypto ? 1 : 0);

    s_instance = new pq_async::connection_pool(max_pool_conn_count);
    s_init = true;
}

pq_async::connection_pool::~connection_pool()
{
    std::map< std::string, std::vector< connection* >* >::iterator pool_it;
    for(pool_it = _pools.begin(); pool_it != _pools.end(); pool_it++){
        std::vector< connection* >* cons = pool_it->second;
        
        for(unsigned int i = 0; i < cons->size(); ++i){
            if((*cons)[i]->_owner)
                (*cons)[i]->_owner->_conn = NULL;
            
            PQ_ASYNC_DEF_DBG(
                "releasing connection '{}' because the connection pool is "
                "destroyed, last modification date is '{}', "
                "connection count is '{}'", 
                (*cons)[i]->id().c_str(),
                hhdate::format("%F %T", (*cons)[i]->_last_modification_date),
                (int)(cons->size() -1)
            );
            
            delete (*cons)[i];
            cons->erase(cons->begin() + i);
        }
        
        delete pool_it->second;
    }
    
    _pools.clear();
}

int32_t pq_async::connection_pool::_get_opened_connection_count(
    const std::string& connection_string)
{
    #ifdef PQ_ASYNC_THREAD_SAFE
    std::unique_lock<std::recursive_mutex> lock(conn_pool_mutex);
    #endif
    
    std::map< std::string, std::vector< connection* >* >::iterator pool_it;

    pool_it = _pools.find(connection_string);
    std::vector< connection* >* cons = NULL;

    if(pool_it == _pools.end()){
        _pools[connection_string] = new std::vector< connection* >();
        cons = _pools[connection_string];
    } else {
        cons = pool_it->second;
    }

    // clean up dead connections higher than 1
    if(cons->size() > 4)
        for(unsigned int i = cons->size() -1; i > 4; --i){
            if((*cons)[i]->is_dead()){
                if((*cons)[i]->_owner)
                    (*cons)[i]->_owner->_conn = NULL;
                
                PQ_ASYNC_DEF_DBG(
                    "releasing connection '{}' because it's dead, "
                    "last modification date is '{}', connection count is '{}'", 
                    (*cons)[i]->id().c_str(),
                    hhdate::format(
                        "%F %T", (*cons)[i]->_last_modification_date
                    ),
                    (int)(cons->size() -1)
                );
                
                delete (*cons)[i];
                cons->erase(cons->begin() + i);
            }
        }
    
    int32_t connCount = 0;
    for(unsigned int i = 0; i < cons->size(); ++i){
        if((*cons)[i]->_res.load() == 1)
            ++connCount;
    }
    
    return connCount;
}


pq_async::connection* pq_async::connection_pool::_get_connection(
    database_t* owner, const std::string& connection_string, int32_t timeout_ms)
{
    #ifdef PQ_ASYNC_THREAD_SAFE
    std::unique_lock<std::recursive_mutex> lock(conn_pool_mutex);
    #endif
    
    std::map< std::string, std::vector< connection* >* >::iterator pool_it;
    
    pool_it = _pools.find(connection_string);
    std::vector< connection* >* cons = NULL;
    
    // get or create new pool if no pool exists for that connection string
    if(pool_it == _pools.end()){
        _pools[connection_string] = new std::vector< connection* >();
        cons = _pools[connection_string];
    } else {
        cons = pool_it->second;
    }
    
    // clean up dead connections higher than 4
    if(cons->size() > 4)
        for(unsigned int i = cons->size() -1; i > 4; --i){
            if((*cons)[i]->is_dead()){
                if((*cons)[i]->_owner)
                    (*cons)[i]->_owner->_conn = NULL;
                
                PQ_ASYNC_DEF_DBG(
                    "releasing connection '{}' because it's dead, "
                    "last modification date is '{}', connection count is '{}'",
                    (*cons)[i]->id().c_str(),
                    hhdate::format(
                        "%F %T", (*cons)[i]->_last_modification_date
                    ),
                    (int)(cons->size() -1)
                );
                
                delete (*cons)[i];
                cons->erase(cons->begin() + i);
            }
        }
    
    // init first conns if it doesn't exist
    int con_size = cons->size();
    if(con_size == 0){
        connection* conn = new connection(this, connection_string);
        cons->push_back(conn);
        
        PQ_ASYNC_DEF_DBG(
            "connection created '{}', connection count is '{}'",
            conn->id().c_str(), (int)cons->size()
        );
        
        if(conn->lock()){
            conn->_owner = owner;
            return conn;
        }
    }
    
    // first try to reuse an existing connection
    if(con_size < _max_conn){
        for(unsigned int i = 0; i < cons->size(); ++i){
            if((*cons)[i]->lock()){
                (*cons)[i]->_owner = owner;
                return (*cons)[i];
            }
        }
    }
    
    // if we have room for more, just create it
    if(con_size < _max_conn){
        connection* conn = new connection(this, connection_string);
        cons->push_back(conn);
        
        if(conn->lock()){
            return conn;
        } else {
            std::string err_msg(
                "pq_async::connection_pool: unable to assign a connection"
            );
            throw pq_async::exception(err_msg);
        }
    }
    
    // finally try to steal a connection starting with 
    //   last id higher than last stolen index
    
    // infinite wait
    if(timeout_ms <= 0)
        timeout_ms = INT32_MAX;
    // connection stealing will timeout in timeout_ms milliseconds
    std::chrono::system_clock::time_point
        timeout_date(
            std::chrono::system_clock::now() -
            std::chrono::milliseconds(timeout_ms)
        );
    
    // sort steelable connections
    std::vector<connection*> scons;
    scons.reserve(cons->size());
    // first try to steal a connection with higher id than last stolen index
    for(size_t i = 0; i < cons->size(); ++i)
        if((*cons)[i]->id() > pq_async::connection_pool::last_stolen_conn_id)
            scons.emplace_back((*cons)[i]);
    // then try to steal a connection with lower id than last stolen index
    for(size_t i = 0; i < cons->size(); ++i)
        if((*cons)[i]->id() <= pq_async::connection_pool::last_stolen_conn_id)
            scons.emplace_back((*cons)[i]);
    
    do{
    //while(timeout_date > std::chrono::system_clock::now()){
        for(size_t i = 0; i < scons.size(); ++i){
            connection* con = scons[i];
            if(con->can_be_stolen()){
                // reasign the connection
                if(con->_owner)
                    con->_owner->_conn = NULL;
                con->_owner = owner;
                con->reserve();
                
                pq_async::connection_pool::last_stolen_conn_id = con->id();
                
                PQ_ASYNC_DEF_DBG(
                    "connection '{}' was stolen, "
                    "connection count is '{}'", 
                    con->id(), (int)cons->size()
                );
                return con;
            }
        }
        
        // sleep for 10ms
        usleep(10000);
    } while(timeout_date > std::chrono::system_clock::now());
    
    // no connection can be steal so throw an error
    std::string err_msg( //TODO: need to put error strings in const...
        "unable to assign a connection because max connection "
        "count reached, connection count is '"
    );
    err_msg += md::num_to_str(
        _get_opened_connection_count(connection_string)
    );
    err_msg += "'";
    throw pq_async::connection_pool_assign_exception(err_msg);
}


} //namespace pq_async