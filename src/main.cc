#include "share.h"
#include "hash.h"
#include "proram.h"
#include "bubbleram.h"


#include <iostream>
#include <emp-tool/emp-tool.h>
#include "link.h"
#include "measure_link.h"
#include "ferret.h"


template <Mode mode>
void part_test() {
  const auto con = [](Zp i) { return Share<mode>::constant(i); };

  std::vector<Share<mode>> xs(8);
  for (std::size_t i = 0; i < 8; ++i) {
    xs[i] = con(i);
  }

  std::vector<bool> choices = { false, true, false, false, false, true, false, false };

  const auto perm = partition<mode, 3, Share<mode>>(choices, xs);

  for (std::size_t i = 0; i < 8; ++i) {
    std::cout << xs[i].data().data() << '\n';
  }


  if constexpr (mode == Mode::Input) {
    for (const auto& p : perm) {
      std::cout << p << '\n';
    }
  }
}


template <Mode mode>
void simple() {
  const auto con = [](Zp i) { return Share<mode>::constant(i); };


  constexpr std::size_t logn = 18;


  /* auto R = BubbleRAM<mode, logn>::fresh({ 3, 2, 1, 0, 0, 1, 2, 3 }); */
  auto R = PrORAM<mode, logn>::fresh({});


  for (std::size_t i = 0; i < (1 << logn); ++i) {
    /* R.access(); */
    R.read();
  }
}


template <typename F>
auto timed(const F& f) {
  auto start = std::chrono::high_resolution_clock::now();
  f();
  auto finish = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = finish - start;
  return elapsed.count();
}



void prover(Link& link) {
  simple<Mode::Input>();

  link.send(reinterpret_cast<const std::byte*>(&n_ots), sizeof(n_ots));
  link.flush();

  ferret_receipts = ferret_recv<Model::Malicious>(link, n_ots);

  // calculate ot offset bits from random ot bits
  std::vector<std::bitset<128>> choice_offset(ferret_choices.size());
  for (std::size_t i = 0; i < n_ots; ++i) {
    choice_offset[i/128][i%128] =
      ferret_choices[i/128][i%128] != ferret_receipts[i][0];
  }

  link.send(reinterpret_cast<const std::byte*>(
        choice_offset.data()),
        choice_offset.size() * 16);


  messages.resize(n_messages*5);
  std::cout << n_messages << '\n';
  link.recv(messages);

  n_ots = 0;
  n_messages = 0;

  hash_init();
  simple<Mode::Prove>();
  std::cout << hash_digest() << '\n';

  // TODO
  /* hash_init(); */
  /* seed(s); */
  /* do { */
  /*   Share<Mode::Check>::delta = draw(); */
  /* } while (Share<Mode::Check>::delta.data() == 0); */
  /* simple<Mode::Check>(); */
}


void verifier(Link& link) {
  std::size_t n_ot;
  link.recv(reinterpret_cast<std::byte*>(&n_ot), sizeof(n_ot));

  ferret_zeros = ferret_send<Model::Malicious>(link, n_ot, ferret_delta);

  ferret_choices.resize((n_ot + 127)/128);
  link.recv(reinterpret_cast<std::byte*>(
        ferret_choices.data()),
      ferret_choices.size() * 16);

  for (std::size_t i = 0; i < n_ot; ++i) {
    if (ferret_choices[i/128][i%128]) { ferret_zeros[i] ^= ferret_delta; }
  }



  const auto s = rand_key();
  hash_init();
  seed(s);
  do {
    Share<Mode::Verify>::delta = draw();
  } while (Share<Mode::Verify>::delta.data() == 0);

  simple<Mode::Verify>();
  std::cout << "NUM OTS: " << n_ots << '\n';
  std::cout << "NUM MESSAGES: " << messages.size() << '\n';
  link.send(messages);

  std::cout << hash_digest() << '\n';
}


int main(int argc, char** argv) {

  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " <P/V> <port>\n";
    std::exit(1);
  }

  const int port = atoi(argv[2]);

  if (strcmp(argv[1], "P") == 0) {
    emp::NetIO io { "127.0.0.1", port };
    NetLink link { &io };
    std::cout << timed([&] {
      prover(link);
    }) << '\n';
  } else if (strcmp(argv[1], "V") == 0) {
    emp::NetIO io { nullptr, port };
    NetLink nlink { &io };
    MeasureLink link { static_cast<Link*>(&nlink) };
    std::cout << timed([&] {
      verifier(link);
    }) << '\n';
    std::cout << "BYTES: " << link.traffic() << "\n";
  } else {
    std::cerr << "usage: " << argv[0] << " <P/V> <port>\n";
    std::exit(1);
  }
}
