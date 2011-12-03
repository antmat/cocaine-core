#include <iomanip>
#include <sstream>

#include <boost/algorithm/string/join.hpp>
#include <boost/assign.hpp>
#include <boost/lexical_cast.hpp>

#include "cocaine/drivers.hpp"
#include "cocaine/engine.hpp"
#include "cocaine/registry.hpp"

using namespace cocaine::engine;
using namespace cocaine::networking;

// Selectors
// ---------

void engine_t::job_queue_t::push(const boost::shared_ptr<job::job_t>& job) {
    if(job->policy().urgent) {
        push_front(job);
        job->process_event(events::enqueued(1));
    } else {
        push_back(job);
        job->process_event(events::enqueued(size()));
    }
    
}

bool engine_t::idle_slave::operator()(pool_map_t::pointer slave) const {
    return slave->second->state_downcast<const slave::idle*>();
}

// Basic stuff
// -----------

engine_t::engine_t(zmq::context_t& context, const std::string& name):
    m_running(false),
    m_context(context),
    m_messages(m_context, ZMQ_ROUTER)
{
    m_app_cfg.name = name;

    syslog(LOG_DEBUG, "engine [%s]: constructing", m_app_cfg.name.c_str());
   
    m_messages.bind(boost::algorithm::join(
        boost::assign::list_of
            (std::string("ipc:///var/run/cocaine"))
            (config_t::get().core.instance)
            (m_app_cfg.name),
        "/")
    );
    
    m_watcher.set<engine_t, &engine_t::message>(this);
    m_watcher.start(m_messages.fd(), ev::READ);
    m_processor.set<engine_t, &engine_t::process>(this);
    m_processor.start();

    m_gc_timer.set<engine_t, &engine_t::cleanup>(this);
    m_gc_timer.start(5.0f, 5.0f);
}

engine_t::~engine_t() {
    if(m_running) {
        stop();
    }

    syslog(LOG_DEBUG, "engine [%s]: destructing", m_app_cfg.name.c_str()); 
}

// Operations
// ----------

Json::Value engine_t::start(const Json::Value& manifest) {
    BOOST_ASSERT(!m_running);

    // Application configuration
    // -------------------------

    m_app_cfg.type = manifest["type"].asString();
    m_app_cfg.args = manifest["args"].asString();

    if(!core::registry_t::instance()->exists(m_app_cfg.type)) {
        throw std::runtime_error("no plugin for '" + m_app_cfg.type + "' is available");
    }
    
    m_app_cfg.version = manifest.get("version", 0).asUInt();

    if(m_app_cfg.version == 0) {
        throw std::runtime_error("no app version has been specified");
    }
    
    // Pool configuration
    // ------------------

    m_policy.backend = manifest["engine"].get("backend",
        config_t::get().engine.backend).asString();
    
    if(m_policy.backend != "thread" && m_policy.backend != "process") {
        throw std::runtime_error("invalid backend type");
    }
    
    m_policy.suicide_timeout = manifest["engine"].get("suicide-timeout",
        config_t::get().engine.suicide_timeout).asDouble();
    m_policy.pool_limit = manifest["engine"].get("pool-limit",
        config_t::get().engine.pool_limit).asUInt();
    m_policy.queue_limit = manifest["engine"].get("queue-limit",
        config_t::get().engine.queue_limit).asUInt();
    
    // Tasks configuration
    // -------------------

    Json::Value tasks(manifest["tasks"]);

    if(!tasks.isNull() && tasks.size()) {
        syslog(LOG_INFO, "engine [%s]: starting", m_app_cfg.name.c_str()); 
    
        std::string endpoint(manifest["pubsub"]["endpoint"].asString());
        
        if(!endpoint.empty()) {
            m_pubsub.reset(new socket_t(m_context, ZMQ_PUB));
            m_pubsub->bind(endpoint);
        }

        Json::Value::Members names(tasks.getMemberNames());

        for(Json::Value::Members::iterator it = names.begin(); it != names.end(); ++it) {
            std::string task(*it);
            std::string type(tasks[task]["type"].asString());
            
            if(type == "recurring-timer") {
                m_tasks.insert(task, new driver::recurring_timer_t(this, task, tasks[task]));
            } else if(type == "filesystem-monitor") {
                m_tasks.insert(task, new driver::filesystem_monitor_t(this, task, tasks[task]));
            } else if(type == "zeromq-server") {
                m_tasks.insert(task, new driver::zeromq_server_t(this, task, tasks[task]));
            } else if(type == "server+lsd") {
                m_tasks.insert(task, new driver::lsd_server_t(this, task, tasks[task]));
            } else if(type == "native-server") {
                m_tasks.insert(task, new driver::native_server_t(this, task, tasks[task]));
            } else {
               throw std::runtime_error("no driver for '" + type + "' is available");
            }
        }
    } else {
        throw std::runtime_error("no tasks has been specified");
    }

    m_running = true;

    return info();
}

Json::Value engine_t::stop() {
    BOOST_ASSERT(m_running);
    
    syslog(LOG_INFO, "engine [%s]: stopping", m_app_cfg.name.c_str()); 

    m_running = false;

    // Abort all the outstanding jobs 
    while(!m_queue.empty()) {
        m_queue.front()->process_event(
            events::server_error("engine is shutting down"));
        m_queue.pop_front();
    }

    // Signal the slaves to terminate
    for(pool_map_t::iterator it = m_pool.begin(); it != m_pool.end(); ++it) {
        m_messages.send_multi(
            boost::make_tuple(
                protect(it->first),
                TERMINATE
            )
        );
        
        it->second->process_event(events::death());
    }

    m_pool.clear();
    m_tasks.clear();
    m_watcher.stop();
    m_processor.stop();
    m_gc_timer.stop();

    return info();
}

namespace {
    struct busy_slave {
        bool operator()(engine_t::pool_map_t::const_pointer slave) {
            return slave->second->state_downcast<const slave::busy*>();
        }
    };
}

Json::Value engine_t::info() const {
    Json::Value results(Json::objectValue);

    if(m_running) {
        results["queue-depth"] = static_cast<Json::UInt>(m_queue.size());
        results["slaves"]["total"] = static_cast<Json::UInt>(m_pool.size());
        
        results["slaves"]["busy"] = static_cast<Json::UInt>(
            std::count_if(
                m_pool.begin(),
                m_pool.end(),
                busy_slave()
            )
        );

        for(task_map_t::const_iterator it = m_tasks.begin(); it != m_tasks.end(); ++it) {
            results["tasks"][it->first] = it->second->info();
        }
    }
    
    results["running"] = m_running;
    results["version"] = m_app_cfg.version;

    return results;
}

void engine_t::enqueue(const boost::shared_ptr<job::job_t>& job, bool overflow) {
    zmq::message_t request;
    
    if(!m_running) {
        job->process_event(events::server_error("engine is shutting down"));
        return;
    }

    request.copy(job->request());

    pool_map_t::iterator it(
        unicast(
            idle_slave(), 
            boost::make_tuple(
                INVOKE,
                job->driver()->method(),
                boost::ref(request)
            )
        )
    );

    // NOTE: If we got an idle slave, then we're lucky and got an instant scheduling;
    // if not, try to spawn more slaves, and enqueue the job.
    if(it != m_pool.end()) {
        it->second->process_event(events::invoked(job));
    } else {
        if(m_pool.empty() || m_pool.size() < m_policy.pool_limit) {
            std::auto_ptr<slave::slave_t> slave;
            
            try {
                if(m_policy.backend == "thread") {
                    slave.reset(new slave::thread_t(this, m_app_cfg.type, m_app_cfg.args));
                } else if(m_policy.backend == "process") {
                    slave.reset(new slave::process_t(this, m_app_cfg.type, m_app_cfg.args));
                }
            } catch(const std::exception& e) {
                syslog(LOG_ERR, "engine [%s]: unable to spawn more workers - %s",
                    m_app_cfg.name.c_str(), e.what());
            }

            std::string slave_id(slave->id());
            m_pool.insert(slave_id, slave);
        } else if(!overflow && (m_queue.size() > m_policy.queue_limit)) {
            syslog(LOG_ERR, "engine [%s]: dropping '%s' job - the queue is full",
                m_app_cfg.name.c_str(), job->driver()->method().c_str());
            job->process_event(events::resource_error("the queue is full"));
            return;
        }
            
        m_queue.push(job);
    }
}

// PubSub Interface
// ----------------

void engine_t::publish(const std::string& key, const Json::Value& object) {
    if(m_pubsub && object.isObject()) {
        zmq::message_t message;
        ev::tstamp now = ev::get_default_loop().now();

        // Disassemble and send in the envelopes
        Json::Value::Members members(object.getMemberNames());

        for(Json::Value::Members::iterator it = members.begin(); it != members.end(); ++it) {
            std::string field(*it);
            
            std::ostringstream envelope;
            envelope << key << " " << field << " " << config_t::get().core.hostname << " "
                     << std::fixed << std::setprecision(3) << now;

            message.rebuild(envelope.str().size());
            memcpy(message.data(), envelope.str().data(), envelope.str().size());
            m_pubsub->send(message, ZMQ_SNDMORE);

            Json::Value value(object[field]);
            std::string result;

            switch(value.type()) {
                case Json::booleanValue:
                    result = value.asBool() ? "true" : "false";
                    break;
                case Json::intValue:
                case Json::uintValue:
                    result = boost::lexical_cast<std::string>(value.asInt());
                    break;
                case Json::realValue:
                    result = boost::lexical_cast<std::string>(value.asDouble());
                    break;
                case Json::stringValue:
                    result = value.asString();
                    break;
                default:
                    result = boost::lexical_cast<std::string>(value);
            }

            message.rebuild(result.size());
            memcpy(message.data(), result.data(), result.size());
            m_pubsub->send(message);
        }
    }
}

// Slave I/O
// ---------

void engine_t::message(ev::io&, int) {
    if(m_messages.pending()) {
        m_watcher.stop();
        m_processor.start();
    }
}

void engine_t::process(ev::idle&, int) {
    if(m_messages.pending()) {
        std::string slave_id;
        unsigned int code = 0;

        boost::tuple<raw<std::string>, unsigned int&> tier(protect(slave_id), code);
        m_messages.recv_multi(tier);

        pool_map_t::iterator slave(m_pool.find(slave_id));

        if(slave != m_pool.end()) {
            const slave::busy* state = slave->second->state_downcast<const slave::busy*>();
            
            switch(code) {
                case CHUNK: {
                    zmq::message_t message;

                    BOOST_ASSERT(state != 0);

                    m_messages.recv(&message);
                    state->job()->process_event(events::response(message));

                    break;
                }
             
                case ERROR: {
                    std::string message;

                    BOOST_ASSERT(state != 0);

                    m_messages.recv(message);
                    state->job()->process_event(events::application_error(message));                    

                    break;
                }

                case CHOKE: {
                    BOOST_ASSERT(state != 0);
                   
                    slave->second->process_event(events::completed());
                    
                    break;
                }

                case SUICIDE:
                    slave->second->process_event(events::death());

                    return;

                case TERMINATE:
                    syslog(LOG_ERR, "engine [%s]: the application seems to be broken",
                        m_app_cfg.name.c_str());
                    
                    stop();
                    
                    return;
            }

            slave->second->process_event(events::heartbeat());

            if(slave->second->state_downcast<const slave::idle*>() && !m_queue.empty()) {
                // NOTE: This will always succeed due to the test above
                enqueue(m_queue.front());
                m_queue.pop_front();
            }

            // TEST: Ensure that there're no more message parts pending on the channel
            BOOST_ASSERT(!m_messages.more());
        } else {
            syslog(LOG_DEBUG, "engine [%s]: dropping type %d message from a dead slave %s", 
                m_app_cfg.name.c_str(), code, slave_id.c_str());
            m_messages.drop_remaining_parts();
        }
    } else {
        m_watcher.start(m_messages.fd(), ev::READ);
        m_processor.stop();
    }
}

void engine_t::cleanup(ev::timer&, int) {
    typedef std::vector<pool_map_t::key_type> corpses_t;
    corpses_t corpses;

    for(pool_map_t::iterator it = m_pool.begin(); it != m_pool.end(); ++it) {
        if(it->second->state_downcast<const slave::dead*>()) {
            corpses.push_back(it->first);
        }
    }

    if(!corpses.empty()) {
        for(corpses_t::iterator it = corpses.begin(); it != corpses.end(); ++it) {
            m_pool.erase(*it);
        }
        
        syslog(LOG_INFO, "engine [%s]: recycled %zu dead %s", 
            m_app_cfg.name.c_str(),
            corpses.size(),
            corpses.size() == 1 ? "slave" : "slaves"
        );
    }
}

publication_t::publication_t(driver::driver_t* parent):
    job::job_t(parent, job::policy_t())
{ }

void publication_t::react(const events::response& event) {
    Json::Reader reader(Json::Features::strictMode());
    Json::Value root;

    if(reader.parse(
        static_cast<const char*>(event.message.data()),
        static_cast<const char*>(event.message.data()) + event.message.size(),
        root))
    {
        m_driver->engine()->publish(m_driver->method(), root);
    } else {
        m_driver->engine()->publish(m_driver->method(),
            helpers::make_json("error", "unable to parse the json"));
    }
}

void publication_t::react(const events::error& event) {
    m_driver->engine()->publish(m_driver->method(),
        helpers::make_json("error", event.message));
}

