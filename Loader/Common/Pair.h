#pragma once

#include "Utilities.h"

template <typename First, typename Second>
class Pair {
public:
    Pair() = default;

    template <typename FirstT, typename SecondT>
    Pair(FirstT&& first, SecondT&& second)
        : first(forward<FirstT>(first))
        , second(forward<SecondT>(second))
    {
    }

    // TODO: SFINAE based on whether FirstT & SecondT are convertable to First & Second
    template <typename FirstT, typename SecondT>
    Pair(const Pair<FirstT, SecondT>& other)
        : first(other.first)
        , second(other.second)
    {
    }

    template <typename FirstT, typename SecondT>
    Pair(Pair<FirstT, SecondT>&& other)
        : first(move(other.first))
        , second(move(other.second))
    {
    }

    template <typename FirstT, typename SecondT>
    Pair& operator=(const Pair<FirstT, SecondT>& other)
    {
        first = other.first;
        second = other.second;

        return *this;
    }

    template <typename FirstT, typename SecondT>
    Pair& operator=(const Pair<FirstT, SecondT>&& other)
    {
        first = move(other.first);
        second = move(other.second);

        return *this;
    }

    First first;
    Second second;
};
