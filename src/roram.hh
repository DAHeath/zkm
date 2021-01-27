#include "roram.h"
#include "permute.h"
#include "draw.h"


template <Mode mode, std::size_t width>
RORAM<mode, width> RORAM<mode, width>::fresh(
    std::size_t n, const std::vector<std::uint32_t>& permutation) {
  if constexpr (mode == Mode::Input) {
    assert(permutation.size() == n);
  }
  std::vector<std::array<Zp, width>> keys;
  if constexpr (mode == Mode::Verify || mode == Mode::Check) {
    keys.resize(n);
  }

  std::vector<std::array<KeyShare<mode>, 2>> pkeys(n);
  for (std::size_t i = 0; i < n; ++i) {
    std::array<Zp, width> k;
    if constexpr (mode == Mode::Verify || mode == Mode::Check) {
      keys[i] = { draw(), draw() };
      k = keys[i];
    }
    pkeys[i] = { KeyShare<mode>::input(k[0]), KeyShare<mode>::input(k[1]) };
  }

  permute<mode>(std::span { permutation }, std::span { pkeys });


  RORAM<mode, width> out;
  out.permutation = permutation;
  out.keys = keys;
  out.permuted_keys = std::move(pkeys);
  if constexpr (mode == Mode::Input || mode == Mode::Prove) {
    out.buffer.resize(n);
  }
  out.w = 0;
  out.r = 0;
  return out;
}


template <Mode mode, std::size_t width>
std::array<Share<mode>, width> RORAM<mode, width>::read() {
  const auto key = permuted_keys[r];

  std::array<Share<mode>, width> out;
  for (std::size_t i = 0; i < width; ++i) {
    if constexpr (mode == Mode::Input) {
      // in input mode, just read the cleartext value stored in the array
      out[i] = Share<mode> { buffer[permutation[r]][i] };
    } else if constexpr (mode == Mode::Prove) {
      // in prove mode, strip off the key and replace it with the key mask
      out[i] = Share<mode> { buffer[permutation[r]][i] + key[i].data() };
    } else {
      // otherwise, use the key mask
      out[i] = Share<mode> { key[i].data() };
    }
  }
  ++r;
  return out;
}


template <Mode mode, std::size_t width>
void RORAM<mode, width>::write(const std::array<Share<mode>, width>& x) {
  Zp key;
  Zp mask;
  for (std::size_t i = 0; i < width; ++i) {
    if constexpr (mode == Mode::Verify || mode == Mode::Check) {
      key = keys[w][i];
      mask = x[i].data();
    }
    const auto keyshare = KeyShare<mode>::input(key, mask);

    if constexpr (mode == Mode::Prove) {
      buffer[w][i] = x[i].data() - keyshare.data();
    } else if constexpr (mode == Mode::Input) {
      buffer[w][i] = x[i].data();
    }
  }

  ++w;
}
