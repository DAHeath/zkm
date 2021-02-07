#ifndef PERMUTE_H__
#define PERMUTE_H__

#include "bswap.h"

#include <span>
#include <array>


template <Mode mode, std::size_t logn, typename T>
void permute(
    std::span<const std::uint32_t> permutation,
    std::span<T>);


#include "permute.hh"


#endif
