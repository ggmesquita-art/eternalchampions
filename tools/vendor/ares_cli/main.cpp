// tools/vendor/ares_cli/main.cpp — driver headless do núcleo do ares (Mega Drive)
// Roda N quadros da ROM e imprime estado. Base para o harness (Fase 3) e para
// o emulador com camadas (P2/N2, DESIGN §11.6).

#include <ares/ares.hpp>
#include <md/md.hpp>
#include <mia/mia.hpp>
#include <nall/main.hpp>

using namespace nall;

struct HeadlessPlatform : ares::Platform {
  std::shared_ptr<vfs::directory> systemPak;
  std::shared_ptr<vfs::directory> cartridgePak;
  u64 frames = 0;
  u32 fbW = 0, fbH = 0;

  auto pak(ares::Node::Object node) -> std::shared_ptr<vfs::directory> override {
    if(node->name() == "Mega Drive") return systemPak;
    if(node->name() == "Mega Drive Cartridge") return cartridgePak;
    return {};
  }

  auto video(ares::Node::Video::Screen screen, const u32* data, u32 pitch,
             u32 width, u32 height) -> void override {
    frames++;
    fbW = width;
    fbH = height;
    (void)screen; (void)data; (void)pitch;
  }
};

auto nall::main(Arguments arguments) -> void {
  if(arguments.size() < 1) {
    print("uso: ares_cli <rom> [frames] [dump-ram] [--start <quadro>]\n");
    return;
  }
  string rom = arguments.take();
  u64 maxFrames = arguments.size() ? strtoull(arguments.take().data(), nullptr, 0) : 60;
  string ramDump = arguments.size() ? arguments.take() : "";
  bool trace = false;
  struct Press { s64 frame; string button; };
  std::vector<Press> presses;
  struct Dump { s64 frame; string path; };
  std::vector<Dump> dumps;
  while(arguments.size()) {
    auto arg = arguments.take();
    if(arg == "--trace") { trace = true; continue; }
    if(arg == "--press") {
      if(arguments.size() < 1) break;
      string spec = arguments.take();
      s64 f = 0;
      string btn;
      if(auto pos = spec.find(":")) {
        f = strtoull(string{spec.data(), (u32)pos()}.data(), nullptr, 0);
        btn = string{spec.data() + pos() + 1};
      } else {
        f = strtoull(spec.data(), nullptr, 0);
        btn = "Start";
      }
      presses.push_back({f, btn});
      continue;
    }
    if(arg == "--dump") {
      if(arguments.size() < 1) break;
      string spec = arguments.take();
      s64 f = 0;
      string path;
      if(auto pos = spec.find(":")) {
        f = strtoull(string{spec.data(), (u32)pos()}.data(), nullptr, 0);
        path = string{spec.data() + pos() + 1};
      }
      if(f >= 0 && path.size()) dumps.push_back({f, path});
      continue;
    }
    break;
  }

  HeadlessPlatform plat;
  ares::platform = &plat;

  ares::Node::System root;
  auto game = mia::Medium::create("Mega Drive");
  auto result = game->load(rom);
  if(result != successful) {
    print("erro: não consegui carregar a ROM\n");
    return;
  }
  print("carregado: ", game->pak->attribute("title"), " (",
        game->pak->attribute("region"), ")\n");

  auto system = mia::System::create("Mega Drive");
  system->load();
  plat.systemPak = system->pak;
  plat.cartridgePak = game->pak;

  if(!ares::MegaDrive::load(root, "[Sega] Mega Drive (NTSC-U)")) {
    print("erro: falha ao criar o sistema\n");
    return;
  }

  if(auto port = root->find<ares::Node::Port>("Cartridge Slot")) {
    port->allocate();
    port->connect();
  }
  if(auto port = root->find<ares::Node::Port>("Controller Port 1")) {
    port->allocate("Fighting Pad");
    port->connect();
  }
  if(auto port = root->find<ares::Node::Port>("Controller Port 2")) {
    port->allocate("Control Pad");
    port->connect();
  }

  root->power();
  auto setButton = [&](const string& btn, bool down) {
    auto fpad = dynamic_cast<ares::MegaDrive::FightingPad*>(
      ares::MegaDrive::controllerPort1.device.get());
    if(fpad) {
      if(btn == "Start") fpad->start->setValue(down);
      else if(btn == "Up") fpad->up->setValue(down);
      else if(btn == "Down") fpad->down->setValue(down);
      else if(btn == "Left") fpad->left->setValue(down);
      else if(btn == "Right") fpad->right->setValue(down);
      else if(btn == "A") fpad->a->setValue(down);
      else if(btn == "B") fpad->b->setValue(down);
      else if(btn == "C") fpad->c->setValue(down);
      else if(btn == "X") fpad->x->setValue(down);
      else if(btn == "Y") fpad->y->setValue(down);
      else if(btn == "Z") fpad->z->setValue(down);
      else if(btn == "Mode") fpad->mode->setValue(down);
      return;
    }
    auto pad = dynamic_cast<ares::MegaDrive::ControlPad*>(
      ares::MegaDrive::controllerPort1.device.get());
    if(!pad) return;
    if(btn == "Start") pad->start->setValue(down);
    else if(btn == "Up") pad->up->setValue(down);
    else if(btn == "Down") pad->down->setValue(down);
    else if(btn == "Left") pad->left->setValue(down);
    else if(btn == "Right") pad->right->setValue(down);
    else if(btn == "A") pad->a->setValue(down);
    else if(btn == "B") pad->b->setValue(down);
    else if(btn == "C") pad->c->setValue(down);
  };
  for(u64 i = 0; i < maxFrames; i++) {
    for(auto& p : presses) {
      if((s64)i == p.frame) setButton(p.button, true);
      if((s64)i == p.frame + 20) setButton(p.button, false);
    }
    ares::MegaDrive::system.run();
    if(trace) {
      auto& bus = ares::MegaDrive::bus;
      n16 fd64 = bus.read(0xFF, 0x00, 0xFFFD64);
      n16 fd60 = bus.read(0xFF, 0x00, 0xFFFD60);
      n16 fd70 = bus.read(0xFF, 0x00, 0xFFFD70);
      print(i, " st=", hex(fd64, 4L), " in=", hex(fd60, 4L),
            " fc=", hex(fd70, 4L), "\n");
    }
    for(auto& d : dumps) {
      if((s64)i == d.frame) {
        auto& bus = ares::MegaDrive::bus;
        static u8 ram[0x10000];
        for(u32 addr = 0; addr < 0x10000; addr += 2) {
          n16 word = bus.read(0xFF, 0x00, 0xFF0000 + addr);
          ram[addr + 0] = word >> 8;
          ram[addr + 1] = word & 0xFF;
        }
        FILE* f = fopen(d.path.data(), "wb");
        if(f) {
          fwrite(ram, 1, 0x10000, f);
          fclose(f);
          print("dump@", i, " → ", d.path, "\n");
        }
      }
    }
  }
  for(auto& p : presses) setButton(p.button, false);

  print("quadros: ", maxFrames, " | video callbacks: ", plat.frames,
        " (", plat.fbW, "x", plat.fbH, ")\n");

  if(ramDump) {
    auto& bus = ares::MegaDrive::bus;
    static u8 ram[0x10000];
    for(u32 addr = 0; addr < 0x10000; addr += 2) {
      n16 word = bus.read(0xFF, 0x00, 0xFF0000 + addr);
      ram[addr + 0] = word >> 8;
      ram[addr + 1] = word & 0xFF;
    }
    FILE* f = fopen(ramDump.data(), "wb");
    if(!f) {
      print("erro: não consegui abrir ", ramDump, "\n");
      return;
    }
    fwrite(ram, 1, 0x10000, f);
    fclose(f);
    print("RAM de trabalho despejada em ", ramDump, " (64 KiB)\n");
  }

  ares::MegaDrive::system.unload();
  root.reset();
}
