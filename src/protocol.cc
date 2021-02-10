#include "protocol.h"
#include "prg.h"

#include <vector>


Link* the_link;


std::bitset<128> ferret_delta;
std::vector<std::bitset<128>> ferret_choices;
std::vector<std::bitset<128>> ferret_zeros;
std::vector<std::bitset<128>> ferret_receipts;

std::size_t n_messages = 0;
std::size_t n_ots = 0;


Hash256 message_hash;


constexpr std::size_t MESSAGE_BUFFER_SIZE = 1 << 18;
std::vector<std::byte> messages(MESSAGE_BUFFER_SIZE*5);


void reset() {
  message_hash = { };
  ferret_delta = 0;
  ferret_zeros.clear();
  ferret_choices.clear();
  ferret_receipts.clear();
  std::fill(messages.begin(), messages.end(), std::byte { 0 });
  n_messages = 0;
  n_ots = 0;
  hash_init();
}


constexpr std::size_t scratch_cap = 10;


void choose() {
}

void send(Zp x) {
  if (n_messages == MESSAGE_BUFFER_SIZE) {
    flush<Mode::Verify>();
  }
  memcpy(messages.data() + 5*n_messages, &x.data(), 5);
  ++n_messages;
}

Zp recv() {
  if (n_messages % MESSAGE_BUFFER_SIZE == 0) {
    flush<Mode::Prove>();
  }
  Zp out;
  memcpy(&out.data(), messages.data() + 5*n_messages, 5);
  ++n_messages;
  return out;
}

void check(Zp x) {
  if (n_messages == MESSAGE_BUFFER_SIZE) {
    flush<Mode::Check>();
  }
  memcpy(messages.data() + 5*n_messages, &x.data(), 5);
  ++n_messages;
}


template <Mode mode>
void flush() {
  if constexpr (mode == Mode::Prove) {
    the_link->recv(messages);
    message_hash(messages);
    n_messages = 0;
  } else if constexpr (mode == Mode::Verify) {
    the_link->send(messages);
    the_link->flush();
    std::fill(messages.begin(), messages.end(), std::byte { 0 });
    n_messages = 0;
  } else if constexpr (mode == Mode::Check) {
    message_hash(messages);
    std::fill(messages.begin(), messages.end(), std::byte { 0 });
    n_messages = 0;
  }
}


template void flush<Mode::Check>();
template void flush<Mode::Input>();
template void flush<Mode::Prove>();
template void flush<Mode::Verify>();


void ot_send(std::span<Zp> corr) {
  const auto n = corr.size();

  static std::array<Zp, scratch_cap> lows;
  static std::array<Zp, scratch_cap> highs;

  assert(n <= scratch_cap);

  draw(ferret_zeros[n_ots], { lows.data(), n });
  draw(ferret_zeros[n_ots] ^ ferret_delta, { highs.data(), n });
  ++n_ots;

  for (std::size_t i = 0; i < n; ++i) {
    send(lows[i] + corr[i] - highs[i]);
    corr[i] = Zp { 0 } - lows[i];
  }
}

void ot_choose(std::size_t n, bool b) {
  if ((n_ots % 128) == 0) {
    ferret_choices.push_back(0);
  }
  ferret_choices[n_ots/128][n_ots%128] = b;
  ++n_ots;
}


std::pair<bool, std::span<Zp>> ot_recv(std::size_t n) {
  static std::array<Zp, scratch_cap> gen;
  assert(n <= scratch_cap);
  std::span<Zp> out { gen.data(), n };

  bool b = ferret_choices[n_ots/128][n_ots%128];
  draw(ferret_receipts[n_ots], out);
  ++n_ots;
  for (std::size_t i = 0; i < n; ++i) {
    const auto diff = recv();
    if (b) {
      out[i] += diff;
    }
  }
  return { b, out };
}

void ot_check(std::span<Zp> corr) {
  const auto n = corr.size();

  static std::array<Zp, scratch_cap> lows;
  static std::array<Zp, scratch_cap> highs;

  assert(n <= scratch_cap);

  auto seed0 = ferret_receipts[n_ots];
  if (ferret_choices[n_ots/128][n_ots%128]) { seed0 ^= ferret_delta; }
  const auto seed1 = seed0 ^ ferret_delta;

  draw(seed0, { lows.data(), n });
  draw(seed1, { highs.data(), n });
  ++n_ots;

  for (std::size_t i = 0; i < n; ++i) {
    check(lows[i] + corr[i] - highs[i]);
    corr[i] = Zp { 0 } - lows[i];
  }

}


// We draw authentication codes randomly


struct ZpPrg {
public:
  ZpPrg() : ptr(128) { }
  ZpPrg(std::bitset<128> s) : prg(s), ptr(128) { }

  Zp operator()() {
    std::uint64_t content = 0;
    do {
      if ((ptr ^ 128) == 0) {
        // refill
        for (auto& r: rand_buffer) {
          r = prg();
        }
        ptr = 0;
      }
      memcpy(&content, (reinterpret_cast<const std::byte*>(rand_buffer.data())) + 5*ptr, 5);
      ++ptr;
    } while (content >= Zp::p);
    return Zp { content };
  }

private:
  Prg prg;
  std::array<std::bitset<128>, 40> rand_buffer;
  std::size_t ptr;
};

ZpPrg the_prg;


void seed(std::bitset<128> s) {
  the_prg = ZpPrg { s };
}

Zp draw() {
  return the_prg();
}




void draw(const std::bitset<128>& seed, std::span<Zp> tar) {
  // WHP, the source will simply contain candidate prime field elements.
  // Therefore, try to extract these for small values of n first, and only
  // generate PRG values if this is impossible.
  if (tar.size() <= 3) {
    for (std::size_t i = 0; i < tar.size(); ++i) {
      memcpy(&(tar[i].data()), ((const char*)&seed) + 5*i, 5);
      if (tar[i].data() >= Zp::p) {
        // if we go out of bounds, use PRF to construct a fresh seed and try
        // again
        draw(Prg { seed }(), tar);
        break;
      }
    }
  } else {
    // if there are more than 3 required primes, split into groups of 3
    Prg prg(seed);
    std::size_t nsubs = (tar.size()+2)/3;
    for (std::size_t i = 0; i < nsubs; ++i) {
      draw(prg(), tar.subspan(3*i, 3));
    }
  }
}

// zero authentication codes are hashed together; the needed functionality follows
Hash256 zero_hash;


void hash_init() {
  zero_hash = { };
}


void hash(Zp z) {
  zero_hash(reinterpret_cast<const std::byte*>(&z.data()), sizeof(std::uint64_t));
}


std::bitset<256> hash_digest() {
  return zero_hash.digest();
}



std::bitset<128> send_commitment(Link& link, const std::bitset<256>& message) {
  Hash256 h;
  const auto k = rand_key();
  h(reinterpret_cast<const std::byte*>(&message), sizeof(std::bitset<256>));
  h(reinterpret_cast<const std::byte*>(&k), sizeof(std::bitset<128>));
  const std::bitset<256> tar = h.digest();
  link.send(reinterpret_cast<const std::byte*>(&tar), sizeof(std::bitset<256>));
  return k;
}

std::bitset<256> recv_commitment(Link& link) {
  std::bitset<256> tar;
  link.recv(reinterpret_cast<std::byte*>(&tar), sizeof(std::bitset<256>));
  return tar;
}

void open_commitment(Link& link, const std::bitset<128>& key) {
  link.send(reinterpret_cast<const std::byte*>(&key), sizeof(std::bitset<128>));
}

bool check_commitment_opening(
    Link& link,
    const std::bitset<256>& expected,
    const std::bitset<256>& actual) {
  std::bitset<128> key;
  link.recv(reinterpret_cast<std::byte*>(&key), sizeof(std::bitset<128>));

  Hash256 h;
  h(reinterpret_cast<const std::byte*>(&expected), sizeof(std::bitset<256>));
  h(reinterpret_cast<const std::byte*>(&key), sizeof(std::bitset<128>));
  const std::bitset<256> tar = h.digest();

  return tar == actual;
}
