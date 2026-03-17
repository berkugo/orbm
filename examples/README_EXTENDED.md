# Extended IDL and Callback Test (Phase 1 & 2)

## Overview

- **Phase 1**: Karmaşık IDL — struct'lar, interface inheritance, `AppExtended.idl` ile Config + DataService.
- **Phase 2**: Client callback — client kendi referansını server'a kaydeder; server periyodik olarak `on_telemetry(struct)` çağırır.

## IDL Yapısı (`idl/AppExtended.idl`)

- **TypesModule**: `SensorData`, `ConfigEntry` struct'ları.
- **BaseModule**: `BaseService` (ping, get_info).
- **ConfigModule::Config**: `BaseService`'ten türetilir; get_value/set_value + **set_entry(get_entry)** ile struct kullanımı.
- **DataServiceModule**:
  - **ClientCallback**: `oneway void on_telemetry(in SensorData data)` — server → client push.
  - **DataService**: BaseService'ten türetilir; submit_sensor/get_sensor + **register_callback(in ClientCallback cb)**.

## Derleme (g++ gerekli)

```bash
cd cpp_test
# TAO/ACE ortamı (ACE_ROOT, PATH'te g++, tao_idl)
make generate   # IDL → stub/skeleton
make all        # server + client
```

`tao_idl` preprocessor için **g++** PATH'te olmalı. Hata alırsanız: `export PATH=/usr/bin:$PATH` (veya g++'ın olduğu dizin).

## Çalıştırma

1. Naming Service ve server'ı başlatın.
2. Client'ı çalıştırın: struct ile set_entry/get_entry, submit_sensor/get_sensor, ping/get_info kullanır; callback kaydeder.
3. Server ~5 saniyede bir client'ın `on_telemetry(SensorData)` metodunu çağırır.

## tao-inspector ile Parse

`tao-inspector --idl ../../cpp_test/idl` ile tüm IDL'ler (AppExtended dahil) parse edilir. Operasyon isimleri (ping, get_info, set_entry, get_entry, submit_sensor, get_sensor, register_callback, on_telemetry) ve parametre tipleri (TypesModule::ConfigEntry, TypesModule::SensorData, ClientCallback) görünür. Struct içerikleri henüz decode edilmez; parametre tipi olarak gösterilir / decode hatası görünebilir.
