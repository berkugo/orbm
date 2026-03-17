#include "HelloS.h"
#include "EchoS.h"
#include "MathS.h"
#include "StreamS.h"
#include "ServiceS.h"
#include "AppExtendedS.h"
#include "orbsvcs/CosNamingC.h"
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>

class Hello_impl : public virtual POA_HelloModule::Hello {
public:
    Hello_impl(CORBA::ORB_ptr orb) : orb_(CORBA::ORB::_duplicate(orb)) {}
    char* say_hello(const char* name) override {
        std::cout << "[Server] Hello::say_hello(" << name << ")" << std::endl;
        return CORBA::string_dup((std::string("Hello, ") + name + "!").c_str());
    }
    CORBA::Long add(CORBA::Long a, CORBA::Long b) override {
        std::cout << "[Server] Hello::add(" << a << "," << b << ")" << std::endl;
        return a + b;
    }
    CORBA::Long sum_ids(const HelloModule::LongSeq& ids) override {
        CORBA::Long sum = 0;
        for (CORBA::ULong i = 0; i < ids.length(); i++) sum += ids[i];
        std::cout << "[Server] Hello::sum_ids(length=" << ids.length() << ") -> " << sum << std::endl;
        return sum;
    }
    void shutdown() override {
        std::cout << "[Server] Hello::shutdown" << std::endl;
        orb_->shutdown(0);
    }
private:
    CORBA::ORB_var orb_;
};

class Echo_impl : public virtual POA_EchoModule::Echo {
public:
    char* echo_string(const char* message) override {
        std::cout << "[Server] Echo::echo_string(" << message << ")" << std::endl;
        return CORBA::string_dup(message);
    }
    void ping() override { std::cout << "[Server] Echo::ping" << std::endl; }
    void heartbeat(const char* sender_id) override {
        std::cout << "[Server] Echo::heartbeat(" << sender_id << ")" << std::endl;
    }
};

class Math_impl : public virtual POA_MathModule::Math {
public:
    CORBA::Long add(CORBA::Long a, CORBA::Long b) override {
        std::cout << "[Server] Math::add(" << a << "," << b << ")" << std::endl;
        return a + b;
    }
    CORBA::LongLong multiply(CORBA::LongLong x, CORBA::LongLong y) override {
        std::cout << "[Server] Math::multiply(" << x << "," << y << ")" << std::endl;
        return x * y;
    }
    CORBA::Float divide(CORBA::Float a, CORBA::Float b) override {
        std::cout << "[Server] Math::divide(" << a << "," << b << ")" << std::endl;
        return (b != 0) ? (a / b) : 0;
    }
    CORBA::Double power(CORBA::Double base, CORBA::Double exp) override {
        std::cout << "[Server] Math::power(" << base << "," << exp << ")" << std::endl;
        double r = 1;
        for (int i = 0; i < (int)exp; i++) r *= base;
        return r;
    }
    CORBA::ULong factorial(CORBA::UShort n) override {
        std::cout << "[Server] Math::factorial(" << n << ")" << std::endl;
        CORBA::ULong r = 1;
        for (CORBA::UShort i = 2; i <= n; i++) r *= i;
        return r;
    }
};

class Config_impl : public virtual POA_ConfigModule::Config {
public:
    void ping() override {
        std::cout << "[Server] Config::ping (BaseService)" << std::endl;
    }
    char* get_info() override {
        std::cout << "[Server] Config::get_info (BaseService)" << std::endl;
        return CORBA::string_dup("ConfigService v1");
    }
    void get_value(const char* key, CORBA::String_out value) override {
        std::cout << "[Server] Config::get_value(" << key << ")" << std::endl;
        auto it = store_.find(key);
        value = CORBA::string_dup(it != store_.end() ? it->second.c_str() : "");
    }
    void set_value(const char* key, const char* value) override {
        std::cout << "[Server] Config::set_value(" << key << "," << value << ")" << std::endl;
        store_[key] = value;
    }
    CORBA::Boolean try_get(const char* key, char*& value) override {
        std::cout << "[Server] Config::try_get(" << key << ")" << std::endl;
        auto it = store_.find(key);
        if (it != store_.end()) { value = CORBA::string_dup(it->second.c_str()); return true; }
        value = CORBA::string_dup(""); return false;
    }
    CORBA::Long get_counter(CORBA::Long_out current) override {
        std::cout << "[Server] Config::get_counter" << std::endl;
        current = counter_++;
        return 0;
    }
    void set_entry(const TypesModule::ConfigEntry& e) override {
        std::cout << "[Server] Config::set_entry(key=" << e.key.in() << ", value=" << e.value.in() << ", ts=" << e.timestamp << ")" << std::endl;
        entry_store_[e.key.in()] = e;
    }
    TypesModule::ConfigEntry* get_entry(const char* key) override {
        std::cout << "[Server] Config::get_entry(" << key << ")" << std::endl;
        TypesModule::ConfigEntry* e = new TypesModule::ConfigEntry();
        auto it = entry_store_.find(key);
        if (it != entry_store_.end()) {
            e->key = CORBA::string_dup(it->second.key.in());
            e->value = CORBA::string_dup(it->second.value.in());
            e->timestamp = it->second.timestamp;
        } else {
            e->key = CORBA::string_dup(key);
            e->value = CORBA::string_dup("");
            e->timestamp = 0;
        }
        return e;
    }
private:
    std::map<std::string, std::string> store_;
    std::map<std::string, TypesModule::ConfigEntry> entry_store_;
    CORBA::Long counter_ = 0;
};

class Stream_impl : public virtual POA_StreamModule::Stream {
public:
    CORBA::Octet read_byte() override {
        std::cout << "[Server] Stream::read_byte" << std::endl;
        return buffer_.empty() ? 0 : buffer_.front();
    }
    void write_byte(CORBA::Octet b) override {
        std::cout << "[Server] Stream::write_byte(" << (int)b << ")" << std::endl;
        buffer_.push_back(b);
    }
    CORBA::Boolean is_ready() override {
        std::cout << "[Server] Stream::is_ready" << std::endl;
        return ready_;
    }
    void set_flag(CORBA::Boolean enabled) override {
        std::cout << "[Server] Stream::set_flag(" << enabled << ")" << std::endl;
        ready_ = enabled;
    }
    CORBA::Long send_buffer(const char* data) override {
        std::cout << "[Server] Stream::send_buffer(" << data << ")" << std::endl;
        return (CORBA::Long)(data ? strlen(data) : 0);
    }
private:
    std::vector<CORBA::Octet> buffer_;
    CORBA::Boolean ready_ = 0;
};

class DataService_impl : public virtual POA_DataServiceModule::DataService {
public:
    DataService_impl() : callback_tick_(0), stop_(false) {
        callback_thread_ = std::thread(&DataService_impl::callback_loop, this);
    }
    ~DataService_impl() {
        stop_ = true;
        if (callback_thread_.joinable()) callback_thread_.join();
    }
    void ping() override {
        std::cout << "[Server] DataService::ping (BaseService)" << std::endl;
    }
    char* get_info() override {
        std::cout << "[Server] DataService::get_info (BaseService)" << std::endl;
        return CORBA::string_dup("DataService v1");
    }
    void submit_sensor(const TypesModule::SensorData& data) override {
        std::cout << "[Server] DataService::submit_sensor(id=" << data.id
                  << ", name=" << data.name.in()
                  << ", value=" << data.value
                  << ", tags=" << data.meta.tags.length()
                  << ", metrics=" << data.metrics.length()
                  << ", samples=" << data.samples.length()
                  << ")" << std::endl;
        sensors_[data.id] = data;
    }
    TypesModule::SensorData* get_sensor(CORBA::Long id) override {
        std::cout << "[Server] DataService::get_sensor(" << id << ")" << std::endl;
        TypesModule::SensorData* s = new TypesModule::SensorData();
        auto it = sensors_.find(id);
        if (it != sensors_.end()) {
            s->id = it->second.id;
            s->name = CORBA::string_dup(it->second.name.in());
            s->value = it->second.value;
            s->meta = it->second.meta;
            s->metrics = it->second.metrics;
            s->samples = it->second.samples;
            s->histogram = it->second.histogram;
            s->spectrum = it->second.spectrum;
        } else {
            s->id = id;
            s->name = CORBA::string_dup("");
            s->value = 0.0;
        }
        return s;
    }
    void register_client(const char* client_id) override {
        std::cout << "[Server] DataService::register_client(" << (client_id ? client_id : "") << ")" << std::endl;
        client_id_ = client_id ? client_id : "";
    }
    void register_callback(DataServiceModule::ClientCallback_ptr cb) override {
        std::cout << "[Server] DataService::register_callback" << std::endl;
        callback_ref_ = DataServiceModule::ClientCallback::_duplicate(cb);
    }
private:
    void callback_loop() {
        while (!stop_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (stop_) break;
            if (!CORBA::is_nil(callback_ref_.in())) {
                TypesModule::SensorData data;
                data.id = callback_tick_++;
                data.name = CORBA::string_dup("server_push");
                data.value = 10.0 + 0.5 * data.id;
                data.meta.source_id = 7;
                data.meta.source_name = CORBA::string_dup("srv-telemetry");
                data.meta.tags.length(3);
                data.meta.tags[0] = CORBA::string_dup("live");
                data.meta.tags[1] = CORBA::string_dup("corba");
                data.meta.tags[2] = CORBA::string_dup("nested");
                data.meta.attributes.length(2);
                data.meta.attributes[0].k = CORBA::string_dup("fw");
                data.meta.attributes[0].v = CORBA::string_dup("1.2.3");
                data.meta.attributes[1].k = CORBA::string_dup("region");
                data.meta.attributes[1].v = CORBA::string_dup("lab");
                data.meta.loc.lat = 41.0082;
                data.meta.loc.lon = 28.9784;
                data.meta.loc.alt = 35.0;

                data.metrics.length(3);
                data.metrics[0].name = CORBA::string_dup("cpu");
                data.metrics[0].value = 0.42 + 0.01 * data.id;
                data.metrics[1].name = CORBA::string_dup("mem");
                data.metrics[1].value = 0.73 + 0.005 * data.id;
                data.metrics[2].name = CORBA::string_dup("qps");
                data.metrics[2].value = 120.0 + data.id;

                data.samples.length(2);
                data.samples[0].t_ms = 1000 + (CORBA::Long)data.id;
                data.samples[0].reading = data.value;
                data.samples[0].payload.length(8);
                for (CORBA::ULong i = 0; i < data.samples[0].payload.length(); i++) {
                    data.samples[0].payload[i] = (CORBA::Octet)(i + (data.id & 0xFF));
                }
                data.samples[1].t_ms = 2000 + (CORBA::Long)data.id;
                data.samples[1].reading = data.value * 1.1;
                data.samples[1].payload.length(12);
                for (CORBA::ULong i = 0; i < data.samples[1].payload.length(); i++) {
                    data.samples[1].payload[i] = (CORBA::Octet)(0xA0 + i);
                }

                data.histogram.length(6);
                for (CORBA::ULong i = 0; i < data.histogram.length(); i++) {
                    data.histogram[i] = (CORBA::Long)(i * i + (data.id % 5));
                }

                data.spectrum.length(5);
                for (CORBA::ULong i = 0; i < data.spectrum.length(); i++) {
                    data.spectrum[i] = 0.1 * (CORBA::Double)i + 0.01 * (CORBA::Double)data.id;
                }
                try {
                    callback_ref_->on_telemetry(data);
                    std::cout << "[Server] DataService callback -> on_telemetry(id=" << data.id << ")" << std::endl;
                } catch (const CORBA::Exception& ex) {
                    std::cerr << "[Server] callback failed: " << ex << std::endl;
                }
                // Push ConfigSnapshot (nested struct + sequence of struct) to client for decode test
                try {
                    TypesModule::ConfigSnapshot cfg;
                    cfg.version = (CORBA::Long)callback_tick_.load();
                    cfg.entries.length(2);
                    cfg.entries[0].key = CORBA::string_dup("server_pushed");
                    cfg.entries[0].value = CORBA::string_dup(("value_for_" + client_id_ + "_tick_" + std::to_string(callback_tick_.load())).c_str());
                    cfg.entries[0].timestamp = (CORBA::Long)callback_tick_.load();
                    cfg.entries[1].key = CORBA::string_dup("nested_seq");
                    cfg.entries[1].value = CORBA::string_dup("second_entry");
                    cfg.entries[1].timestamp = (CORBA::Long)(callback_tick_.load() + 1);
                    cfg.meta.inner.id = 1000 + (CORBA::Long)callback_tick_.load();
                    cfg.meta.inner.tag = CORBA::string_dup("config_snapshot_meta");
                    cfg.meta.count = 2;
                    callback_ref_->on_config(cfg);
                    std::cout << "[Server] DataService callback -> on_config(version=" << cfg.version
                              << ", entries=" << cfg.entries.length()
                              << ", meta.inner.id=" << cfg.meta.inner.id
                              << ", meta.count=" << cfg.meta.count << ")" << std::endl;
                } catch (const CORBA::Exception& ex) {
                    std::cerr << "[Server] on_config callback failed: " << ex << std::endl;
                }
            }
        }
    }
    std::map<CORBA::Long, TypesModule::SensorData> sensors_;
    std::string client_id_;
    DataServiceModule::ClientCallback_var callback_ref_;
    std::thread callback_thread_;
    std::atomic<CORBA::Long> callback_tick_;
    std::atomic<bool> stop_;
};

class Service_impl : public virtual POA_ServiceModule::Service {
public:
    Service_impl(CORBA::ORB_ptr orb) : orb_(CORBA::ORB::_duplicate(orb)) {}
    char* resolve(const char* name) override {
        std::cout << "[Server] Service::resolve(" << name << ")" << std::endl;
        return CORBA::string_dup(("ior:fake-" + std::string(name)).c_str());
    }
    void _cxx_register(const char* name, const char* ior) override {
        std::cout << "[Server] Service::register(" << name << "," << ior << ")" << std::endl;
        registry_[name] = ior;
    }
    void notify(const char* event) override {
        std::cout << "[Server] Service::notify(" << event << ")" << std::endl;
    }
    CORBA::Short get_status() override {
        std::cout << "[Server] Service::get_status" << std::endl;
        return 0;
    }
    void shutdown() override {
        std::cout << "[Server] Service::shutdown" << std::endl;
        orb_->shutdown(0);
    }
private:
    CORBA::ORB_var orb_;
    std::map<std::string, std::string> registry_;
};

static void bind_one(CosNaming::NamingContext_var& ctx, const char* name, CORBA::Object_ptr obj) {
    CosNaming::Name n(1);
    n.length(1);
    n[0].id = CORBA::string_dup(name);
    n[0].kind = CORBA::string_dup("");
    ctx->rebind(n, obj);
    std::cout << "[Server] Registered: " << name << std::endl;
}

// İşyeri senaryosu: objeler context altında (örn. DEV/ROOT) register edilir.
// path_context = "DEV/ROOT" gibi; servisler bu context altında bind edilir.
static CosNaming::NamingContext_var get_or_create_context(
    CosNaming::NamingContext_var root_ctx, const char* path_context
) {
    if (!path_context || *path_context == '\0')
        return CosNaming::NamingContext::_duplicate(root_ctx.in());
    std::vector<std::string> segments;
    const char* p = path_context;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != '/') p++;
        segments.push_back(std::string(start, p - start));
    }
    if (segments.empty())
        return CosNaming::NamingContext::_duplicate(root_ctx.in());

    CosNaming::NamingContext_var ctx = CosNaming::NamingContext::_duplicate(root_ctx.in());
    for (size_t i = 0; i < segments.size(); i++) {
        CosNaming::Name n(1);
        n.length(1);
        n[0].id = CORBA::string_dup(segments[i].c_str());
        n[0].kind = CORBA::string_dup("");
        CORBA::Object_var obj;
        try {
            obj = ctx->resolve(n);
        } catch (const CosNaming::NamingContext::NotFound&) {
            obj = ctx->bind_new_context(n);
            std::cout << "[Server] Created context: " << segments[i] << std::endl;
        }
        ctx = CosNaming::NamingContext::_narrow(obj.in());
        if (CORBA::is_nil(ctx.in())) {
            std::cerr << "[Server] Failed to get context for " << segments[i] << std::endl;
            return CosNaming::NamingContext::_nil();
        }
    }
    return ctx;
}

int main(int argc, char* argv[]) {
    try {
        CORBA::ORB_var orb = CORBA::ORB_init(argc, argv);
        CORBA::Object_var poa_obj = orb->resolve_initial_references("RootPOA");
        PortableServer::POA_var root_poa = PortableServer::POA::_narrow(poa_obj.in());
        PortableServer::POAManager_var poa_mgr = root_poa->the_POAManager();

        Hello_impl* hello = new Hello_impl(orb.in());
        Echo_impl* echo = new Echo_impl();
        Math_impl* math = new Math_impl();
        Config_impl* config = new Config_impl();
        Stream_impl* stream = new Stream_impl();
        Service_impl* svc = new Service_impl(orb.in());
        DataService_impl* data_svc = new DataService_impl();

        PortableServer::ServantBase_var h(hello), e(echo), m(math), c(config), s(stream), v(svc), d(data_svc);

        PortableServer::ObjectId_var oid_hello = root_poa->activate_object(hello);
        PortableServer::ObjectId_var oid_echo  = root_poa->activate_object(echo);
        PortableServer::ObjectId_var oid_math  = root_poa->activate_object(math);
        PortableServer::ObjectId_var oid_config = root_poa->activate_object(config);
        PortableServer::ObjectId_var oid_stream = root_poa->activate_object(stream);
        PortableServer::ObjectId_var oid_svc   = root_poa->activate_object(svc);
        PortableServer::ObjectId_var oid_data   = root_poa->activate_object(data_svc);

        CORBA::Object_var obj_hello  = root_poa->id_to_reference(oid_hello.in());
        CORBA::Object_var obj_echo   = root_poa->id_to_reference(oid_echo.in());
        CORBA::Object_var obj_math   = root_poa->id_to_reference(oid_math.in());
        CORBA::Object_var obj_config = root_poa->id_to_reference(oid_config.in());
        CORBA::Object_var obj_stream = root_poa->id_to_reference(oid_stream.in());
        CORBA::Object_var obj_svc    = root_poa->id_to_reference(oid_svc.in());
        CORBA::Object_var obj_data   = root_poa->id_to_reference(oid_data.in());

        CORBA::Object_var ns_obj = orb->resolve_initial_references("NameService");
        CosNaming::NamingContext_var root_ctx = CosNaming::NamingContext::_narrow(ns_obj.in());
        if (CORBA::is_nil(root_ctx.in())) { std::cerr << "NameService nil" << std::endl; return 1; }

        // Objeleri DEV/ROOT context altında register et (işyeri senaryosu)
        const char* CONTEXT_PATH = "DEV/ROOT";
        CosNaming::NamingContext_var ctx = get_or_create_context(root_ctx.in(), CONTEXT_PATH);
        if (CORBA::is_nil(ctx.in())) { std::cerr << "Failed to get context " << CONTEXT_PATH << std::endl; return 1; }
        std::cout << "[Server] Binding services under " << CONTEXT_PATH << "/" << std::endl;

        bind_one(ctx, "HelloService", obj_hello.in());
        bind_one(ctx, "EchoService",  obj_echo.in());
        bind_one(ctx, "MathService",  obj_math.in());
        bind_one(ctx, "ConfigService", obj_config.in());
        bind_one(ctx, "StreamService", obj_stream.in());
        bind_one(ctx, "AppService",   obj_svc.in());
        bind_one(ctx, "DataService",  obj_data.in());

        poa_mgr->activate();
        std::cout << "[Server] Running (7 services). Ctrl+C to stop." << std::endl;
        orb->run();

        root_poa->destroy(1, 1);
        orb->destroy();
    }
    catch (const CORBA::Exception& ex) {
        std::cerr << "[Server] CORBA: " << ex << std::endl;
        return 1;
    }
    return 0;
}
