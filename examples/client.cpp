#include "HelloC.h"
#include "EchoC.h"
#include "MathC.h"
#include "StreamC.h"
#include "ServiceC.h"
#include "AppExtendedC.h"
#include "AppExtendedS.h"
#include "orbsvcs/CosNamingC.h"
#include "tao/PortableServer/PortableServer.h"
#include <iostream>
#include <unistd.h>
#include <thread>
#include <vector>
#include <string>

// Callback servant: server calls on_telemetry(struct) and on_config(ConfigEntry) periodically (Phase 2)
class Callback_impl : public virtual POA_DataServiceModule::ClientCallback {
public:
    void on_telemetry(const TypesModule::SensorData& data) override {
        std::cout << "[Client] CALLBACK on_telemetry(id=" << data.id
                  << ", name=" << data.name.in()
                  << ", value=" << data.value
                  << ", src=" << data.meta.source_name.in()
                  << ", tags=" << data.meta.tags.length()
                  << ", metrics=" << data.metrics.length()
                  << ", samples=" << data.samples.length()
                  << ")" << std::endl;
        if (data.metrics.length() > 0) {
            std::cout << "  metric0=" << data.metrics[0].name.in() << ":" << data.metrics[0].value << std::endl;
        }
        if (data.samples.length() > 0) {
            std::cout << "  sample0.t_ms=" << data.samples[0].t_ms
                      << " reading=" << data.samples[0].reading
                      << " payload_len=" << data.samples[0].payload.length()
                      << std::endl;
        }
    }
    void on_config(const TypesModule::ConfigSnapshot& cfg) override {
        std::cout << "[Client] CALLBACK on_config(version=" << cfg.version
                  << ", entries=" << cfg.entries.length()
                  << ", meta.inner.id=" << cfg.meta.inner.id
                  << ", meta.inner.tag=" << cfg.meta.inner.tag.in()
                  << ", meta.count=" << cfg.meta.count
                  << ")" << std::endl;
        for (CORBA::ULong i = 0; i < cfg.entries.length(); i++) {
            std::cout << "  entry[" << i << "] key=" << cfg.entries[i].key.in()
                      << " value=" << cfg.entries[i].value.in()
                      << " ts=" << cfg.entries[i].timestamp << std::endl;
        }
    }
};

// Tek isim (root’ta): "HelloService"
static CORBA::Object_var resolve_name(CosNaming::NamingContext_var& ctx, const char* name) {
    CosNaming::Name n(1);
    n.length(1);
    n[0].id = CORBA::string_dup(name);
    n[0].kind = CORBA::string_dup("");
    return ctx->resolve(n);
}

// İşyeri senaryosu: context path ile resolve, örn. "DEV/ROOT/HelloService"
static CORBA::Object_var resolve_path(CosNaming::NamingContext_var& ctx, const char* path) {
    if (!path || *path == '\0') return CORBA::Object::_nil();
    std::vector<std::string> segments;
    const char* p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != '/') p++;
        segments.push_back(std::string(start, p - start));
    }
    if (segments.empty()) return CORBA::Object::_nil();
    CosNaming::Name n(static_cast<CORBA::ULong>(segments.size()));
    n.length(static_cast<CORBA::ULong>(segments.size()));
    for (size_t i = 0; i < segments.size(); i++) {
        n[static_cast<CORBA::ULong>(i)].id = CORBA::string_dup(segments[i].c_str());
        n[static_cast<CORBA::ULong>(i)].kind = CORBA::string_dup("");
    }
    return ctx->resolve(n);
}

static void do_round(
    HelloModule::Hello_var& hello,
    EchoModule::Echo_var& echo,
    MathModule::Math_var& math,
    ConfigModule::Config_var& config,
    StreamModule::Stream_var& stream,
    ServiceModule::Service_var& svc,
    DataServiceModule::DataService_var& data_svc,
    int round
) {
    std::cout << "[Client] --- Round " << round << " ---" << std::endl;

    CORBA::String_var r = hello->say_hello("World");
    std::cout << "[Client] Hello::say_hello -> " << r.in() << std::endl;
    CORBA::Long sum = hello->add(17 + round, 25);
    std::cout << "[Client] Hello::add -> " << sum << std::endl;
    HelloModule::LongSeq ids;
    ids.length(4);
    ids[0] = 1; ids[1] = 2; ids[2] = 3; ids[3] = 4 + round;
    sum = hello->sum_ids(ids);
    std::cout << "[Client] Hello::sum_ids([1,2,3," << (4+round) << "]) -> " << sum << std::endl;

    r = echo->echo_string("Echo test");
    echo->ping();
    echo->heartbeat("client-1");

    sum = math->add(10, 20);
    CORBA::LongLong mul = math->multiply(100, 5);
    CORBA::Float div = math->divide(10.0f, 4.0f);
    CORBA::Double pow = math->power(2.0, 10.0);
    CORBA::ULong fact = math->factorial(5);

    config->ping();
    CORBA::String_var info = config->get_info();
    std::cout << "[Client] Config::get_info -> " << info.in() << std::endl;
    config->set_value("key1", "value1");
    TypesModule::ConfigEntry entry;
    entry.key = CORBA::string_dup("struct_key");
    entry.value = CORBA::string_dup("struct_value");
    entry.timestamp = (CORBA::Long)round;
    config->set_entry(entry);
    TypesModule::ConfigEntry_var got = config->get_entry("struct_key");
    std::cout << "[Client] Config::get_entry -> key=" << got->key.in() << " value=" << got->value.in() << " ts=" << got->timestamp << std::endl;
    CORBA::String_var out_val;
    config->get_value("key1", out_val.out());
    CORBA::Long ctr;
    config->get_counter(CORBA::Long_out(ctr));

    TypesModule::SensorData sensor;
    sensor.id = 100 + round;
    sensor.name = CORBA::string_dup("temperature");
    sensor.value = 23.5 + 0.1 * round;
    sensor.meta.source_id = 99;
    sensor.meta.source_name = CORBA::string_dup("client-1");
    sensor.meta.tags.length(2);
    sensor.meta.tags[0] = CORBA::string_dup("submit");
    sensor.meta.tags[1] = CORBA::string_dup("demo");
    sensor.meta.attributes.length(1);
    sensor.meta.attributes[0].k = CORBA::string_dup("round");
    {
        std::string round_s = std::to_string(round);
        sensor.meta.attributes[0].v = CORBA::string_dup(round_s.c_str());
    }
    sensor.meta.loc.lat = 40.0 + 0.01 * round;
    sensor.meta.loc.lon = 29.0 + 0.02 * round;
    sensor.meta.loc.alt = 12.0;
    sensor.metrics.length(1);
    sensor.metrics[0].name = CORBA::string_dup("temp");
    sensor.metrics[0].value = sensor.value;
    sensor.samples.length(1);
    sensor.samples[0].t_ms = 1234 + round;
    sensor.samples[0].reading = sensor.value;
    sensor.samples[0].payload.length(4);
    sensor.samples[0].payload[0] = 0xDE;
    sensor.samples[0].payload[1] = 0xAD;
    sensor.samples[0].payload[2] = 0xBE;
    sensor.samples[0].payload[3] = 0xEF;
    sensor.histogram.length(3);
    sensor.histogram[0] = 1; sensor.histogram[1] = 2; sensor.histogram[2] = 3;
    sensor.spectrum.length(3);
    sensor.spectrum[0] = 0.1; sensor.spectrum[1] = 0.2; sensor.spectrum[2] = 0.3;
    data_svc->submit_sensor(sensor);
    TypesModule::SensorData_var back = data_svc->get_sensor(100 + round);
    std::cout << "[Client] DataService::get_sensor -> id=" << back->id
              << " name=" << back->name.in()
              << " value=" << back->value
              << " tags=" << back->meta.tags.length()
              << " metrics=" << back->metrics.length()
              << " samples=" << back->samples.length()
              << std::endl;

    stream->write_byte(0x41);
    CORBA::Octet b = stream->read_byte();
    stream->set_flag(1);
    CORBA::Boolean ready = stream->is_ready();
    CORBA::Long len = stream->send_buffer("data");

    r = svc->resolve("Foo");
    svc->_cxx_register("Bar", "ior:xyz");
    svc->notify("event1");
    CORBA::Short st = svc->get_status();
    (void)st; (void)mul; (void)div; (void)pow; (void)fact; (void)out_val; (void)ctr; (void)b; (void)ready; (void)len;
}

int main(int argc, char* argv[]) {
    try {
        CORBA::ORB_var orb = CORBA::ORB_init(argc, argv);
        CORBA::Object_var ns_obj = orb->resolve_initial_references("NameService");
        CosNaming::NamingContext_var ctx = CosNaming::NamingContext::_narrow(ns_obj.in());
        if (CORBA::is_nil(ctx.in())) {
            std::cerr << "[Client] NameService nil" << std::endl;
            return 1;
        }
        std::cout << "[Client] Connected to Naming Service." << std::endl;

        // İşyeri senaryosu: objeler DEV/ROOT/ altında; path ile resolve
        const char* BASE_PATH = "DEV/ROOT";
        auto resolve_svc = [&ctx, &BASE_PATH](const char* name) -> CORBA::Object_var {
            std::string path = std::string(BASE_PATH) + "/" + name;
            return resolve_path(ctx, path.c_str());
        };

        HelloModule::Hello_var hello = HelloModule::Hello::_narrow(resolve_svc("HelloService").in());
        if (CORBA::is_nil(hello.in())) { std::cerr << "HelloService nil (path " << BASE_PATH << "/HelloService)" << std::endl; return 1; }
        EchoModule::Echo_var echo = EchoModule::Echo::_narrow(resolve_svc("EchoService").in());
        if (CORBA::is_nil(echo.in())) { std::cerr << "EchoService nil" << std::endl; return 1; }
        MathModule::Math_var math = MathModule::Math::_narrow(resolve_svc("MathService").in());
        if (CORBA::is_nil(math.in())) { std::cerr << "MathService nil" << std::endl; return 1; }
        ConfigModule::Config_var config = ConfigModule::Config::_narrow(resolve_svc("ConfigService").in());
        if (CORBA::is_nil(config.in())) { std::cerr << "ConfigService nil" << std::endl; return 1; }
        StreamModule::Stream_var stream = StreamModule::Stream::_narrow(resolve_svc("StreamService").in());
        if (CORBA::is_nil(stream.in())) { std::cerr << "StreamService nil" << std::endl; return 1; }
        ServiceModule::Service_var svc = ServiceModule::Service::_narrow(resolve_svc("AppService").in());
        if (CORBA::is_nil(svc.in())) { std::cerr << "AppService nil" << std::endl; return 1; }
        DataServiceModule::DataService_var data_svc = DataServiceModule::DataService::_narrow(resolve_svc("DataService").in());
        if (CORBA::is_nil(data_svc.in())) { std::cerr << "DataService nil" << std::endl; return 1; }

        // Phase 2: register client callback so server can call us with a struct
        CORBA::Object_var poa_obj = orb->resolve_initial_references("RootPOA");
        PortableServer::POA_var root_poa = PortableServer::POA::_narrow(poa_obj.in());
        PortableServer::POAManager_var poa_mgr = root_poa->the_POAManager();
        Callback_impl* callback_servant = new Callback_impl();
        PortableServer::ServantBase_var cb_guard(callback_servant);
        PortableServer::ObjectId_var oid = root_poa->activate_object(callback_servant);
        CORBA::Object_var callback_obj = root_poa->id_to_reference(oid.in());
        DataServiceModule::ClientCallback_var callback_ref = DataServiceModule::ClientCallback::_narrow(callback_obj.in());
        poa_mgr->activate();
        data_svc->register_client("client-1");
        data_svc->register_callback(callback_ref.in());
        std::cout << "[Client] Registered and callback installed; server will push telemetry and config every ~5s." << std::endl;

        std::thread orb_thread([&orb]() { orb->run(); });

        std::cout << "[Client] All services resolved. Sending traffic every 10 seconds (Ctrl+C to stop)." << std::endl;

        int round = 0;
        for (;;) {
            do_round(hello, echo, math, config, stream, svc, data_svc, ++round);
            sleep(10);
        }

        orb->shutdown(0);
        if (orb_thread.joinable()) orb_thread.join();
        orb->destroy();
    }
    catch (const CORBA::Exception& ex) {
        std::cerr << "[Client] CORBA: " << ex << std::endl;
        return 1;
    }
    return 0;
}
