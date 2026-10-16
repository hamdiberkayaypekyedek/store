# PKG Mağaza

GoldHEN yüklü PS4'te çalışan, store görünümlü bir fake pkg kurucu. Kapaklar ızgarada dizilir; bir içeriğe girip **✕ İndir** dediğinizde pkg, PS4'ün kendi indirme kuyruğuna eklenir (Bildirimler › İndirmeler).

Katalog, bilgisayarınızda (veya kendi sunucunuzda) barındırdığınız bir `catalog.json` dosyasından okunur. Uygulama içinde hazır kaynak yoktur.

```
PC (pkg klasörü + catalog.json)  ──http──▶  PS4: PKG Mağaza  ──▶  BGFT indirme servisi
     tools/serve.py                                  └─(yedek)─▶  Remote Package Installer :12800
```

## 1. Derleme ortamı (bir kez)

1. **WSL Ubuntu** kurun (PowerShell, yönetici olarak): `wsl --install`
2. WSL içinde gerekli araçlar:
   ```bash
   sudo apt update && sudo apt install -y clang lld make
   ```
3. [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases)'in son Linux sürümünü indirip açın. Ardından `~/.bashrc` dosyasına şunu ekleyin:
   ```bash
   export OO_PS4_TOOLCHAIN=/opt/OpenOrbis/PS4Toolchain
   ```
4. Windows'ta bu klasörde hazırlık betiğini çalıştırın. Betik stb başlıklarını indirir, fontları kopyalar, `right.sprx` dosyasını bulur ve ikonları üretir:
   ```powershell
   powershell -ExecutionPolicy Bypass -File setup.ps1
   ```
   `right.sprx` bulunamazsa toolchain'deki `samples/*/sce_sys/about/right.sprx` dosyasını `sce_sys/about/` altına kopyalayın.

## 2. Derleme

```bash
cd /mnt/c/.../pkg-magaza   # bu klasör
make
```

Çıktı `IV0000-PKGM00001_00-PKGMAGAZA0000000.pkg` olur. Bu dosyayı GoldHEN'in pkg kurucusuyla (USB veya Remote Package Installer) PS4'e kurun.

## 3. Katalog sunucusu (PC)

Pkg dosyalarınızın olduğu klasör için katalog üretip sunucuyu başlatın (Python 3 gerekir):

```bash
python tools/make_catalog.py D:\pkgler --name "Mağazam"
python tools/serve.py D:\pkgler
```

`make_catalog.py` her pkg'nin içinden başlığı, sürümü, ID'yi, kapağı (icon0) ve arka planı (pic1) çıkarır. `serve.py` ise PS4'ün parça parça indirme (Range) isteklerini destekler ve ekrana yazılacak ayar satırını gösterir.

## 4. PS4 ayarı

İlk açılışta `/data/pkgstore/config.ini` dosyası oluşturulur. Bu dosyayı FTP ile (GoldHEN FTP, port 2121) düzenleyin:

```ini
catalog_url=http://192.168.1.10:8000/catalog.json
installer=auto        # auto | bgft | rpi
```

`catalog_url` boş bırakılırsa `/data/pkgstore/catalog.json` dosyası kullanılır. Değişiklikten sonra uygulamada **OPTIONS** tuşuna basmanız yeterli.

## Disk Kopyalama (1. adım: disk bilgisi)

Sekmelerin sonunda **Disk Kopyalama** sekmesi bulunur. Katalog yüklenemese bile hata ekranında R1'e basarak bu sekmeye geçebilirsiniz. Sekme açıkken uygulama diski 3 saniyede bir tarar ve şunları gösterir:

- Oyun adı, ürün kodu (CUSA…), sürüm ve boyut
- İçerik kimliği ve bilginin okunduğu dosya
- Oyunun konsolda kurulu olup olmadığı (`/user/app/<ID>/app.pkg`)
- Kapak ikonu

Bilgi şu sırayla aranır:

1. `/mnt/disc/sce_sys/param.sfo`
2. `/mnt/disc` kökündeki ya da bir alt klasöründeki `.pkg` dosyasının param.sfo ve icon0 girdileri
3. Klasör adı (`CUSA12345` gibi)

Kurulu kopya varsa başlık ve ikon `/system_data/priv/appmeta/<ID>/` klasöründen de okunur.

Bu klasörler korumalı alandaki (sandbox) bir uygulamaya kapalıdır. Bu yüzden disk okuma için uygulamayı **GoldHEN Plugins SDK** ile derleyin:

```bash
make GOLDHEN_SDK=/opt/GoldHEN_Plugins_SDK
```

SDK olmadan derlenirse ekranda "korumalı alanda çalışıyor" uyarısı çıkar. Her diskte kökün ve alt klasörlerin listesi `log.txt` dosyasına yazılır. Disk okunamazsa bu kısmı gönderin; diskin gerçek düzenine göre okuma sırasını düzeltelim.

Kopyalamanın kendisi henüz yok. Düğme görünür ama devre dışı.

## Otomatik güncelleme (GitHub)

Uygulama açılışta GitHub'daki `version.json` dosyanıza bakar. Daha yeni bir sürüm varsa "Güncelleme mevcut" ekranı çıkar; ✕ ile yeni pkg indirilir (mevcut kurulum yöntemiyle). Böylece uygulamayı her değiştirdiğimizde flash bellek gerekmez.

> Bu yalnızca **uygulamanın kendi** güncellemesidir. Oyun kataloğu ayrı bir dosyadır (`catalog.json`) ve onu siz doldurursunuz.

GitHub HTTPS kullandığı için sürüm kontrolü PS4'ün kendi `libSceHttp`'si ile yapılır (pkg indirmesi ise BGFT ile). Kapatmak için `config.ini` içindeki `update_url` satırını boş bırakın.

### Git ve depo kurulumu (bir kez)

1. **Git'i kurun:** <https://git-scm.com/download/win> (kurulumda varsayılanlar yeterli). İsteğe bağlı ama yayın betiği için önerilir: **GitHub CLI** <https://cli.github.com>.
2. Bu klasörü bir depo yapın ve GitHub'a bağlayın:
   ```bash
   git init
   git add .
   git commit -m "İlk sürüm"
   ```
3. GitHub'da boş bir depo açın (Settings değil, sağ üst **+ › New repository**). Herkese açık (public) olması, PS4'ün `version.json`'a ve release pkg'sine erişebilmesi için en kolayıdır. Sonra:
   ```bash
   git remote add origin https://github.com/hamdiberkayaypekyedek/store.git
   git branch -M main
   git push -u origin main
   ```
4. `config.ini` içindeki `update_url` satırı şu adrese bakar (uygulama ilk açılışta bunu kendisi yazar):
   ```
   update_url=https://raw.githubusercontent.com/hamdiberkayaypekyedek/store/main/version.json
   ```

### Yeni sürüm yayınlama

Yeni bir pkg derledikten sonra (GitHub CLI kuruluysa):

```bash
powershell -ExecutionPolicy Bypass -File release\yayinla.ps1 -Pkg .\IV0000-PKGM00001_00-PKGMAGAZA0000000.pkg -Version 01.01 -Notlar "Disk kopyalama eklendi"
```

Betik pkg'yi GitHub release olarak yükler, `version.json`'u günceller ve repoya gönderir. PS4'te uygulamayı açınca güncelleme ekranı çıkar.

Elle yapmak isterseniz: pkg'yi `v01.01` etiketli bir release'e yükleyin, `version.json` içindeki `version`, `pkg` (release indirme adresi), `size` ve `notes` alanlarını güncelleyip `git push` yapın.

## Kontroller

| Tuş | Izgarada | Detay sayfasında |
|---|---|---|
| Yön tuşları | Gezin (basılı tutunca hızlanır) | – |
| ✕ | Detayı aç | İndir / kur |
| ○ | – | Geri |
| L1 / R1 | Kategori / Disk Kopyalama sekmesi | – |
| OPTIONS | Ayarı ve kataloğu yeniden yükle | – |

Güncelleme ekranında: ✕ şimdi güncelle, ○ daha sonra.

## Katalog biçimi

```json
{
  "store_name": "Mağazam",
  "categories": [
    {
      "name": "Homebrew",
      "items": [
        {
          "id": "CUSA00000",
          "title": "Örnek Uygulama",
          "publisher": "Ben",
          "version": "01.00",
          "size": 52428800,
          "cover": "covers/ornek.png",
          "background": "covers/ornek_bg.png",
          "pkg": "pkg/ornek.pkg",
          "package_type": "PS4GD",
          "description": "Açıklama metni."
        }
      ]
    }
  ]
}
```

- Göreli adresler `catalog.json` dosyasının konumuna göre çözülür.
- Sadece `http://` desteklenir; HTTPS desteklenmez.
- Kapaklar PNG ya da JPEG olabilir.

## Sorun giderme

Her şey `/data/pkgstore/log.txt` dosyasına yazılır. Sorun olursa bu dosyayı FTP ile alıp paylaşın.

| Belirti | Bakılacak yer |
|---|---|
| Metin görünmüyor | `assets/font.ttf` pkg içinde mi? Log'da `font: normal=0` yazıyor mu? |
| "Bağlanılamadı" | PC güvenlik duvarı 8000 portunu engelliyor olabilir. PS4 ve PC aynı ağda mı? |
| "libSceBgft yüklenemedi" / "BGFT başlatılamadı" | Uygulama yeterli yetkiye sahip değil. Remote Package Installer'ı açın ve `installer=rpi` kullanın. |
| Link hatası (`undefined symbol`) | Makefile'daki `LIBS` satırını toolchain'in SDL2 örneğiyle karşılaştırın. |

## Bilinen belirsizlikler (cihazda doğrulanmadı)

- **BGFT yapı düzeni ve izinler:** Remote Package Installer'ın açık kaynak düzeni temel alındı. Uygulama yetkisi yetmezse kurulum otomatik olarak RPI'ye (port 12800) düşer.
- **`package_type`:** Oyunlar için `PS4GD` kullanılıyor. Güncelleme (`PS4DP`) ve DLC (`PS4AC`) değerleri tahmindir.
- **SDL2 portu:** OpenOrbis'teki SDL2 portuyla yazıldı. Gamepad doğrudan `scePad` üzerinden okunuyor.
