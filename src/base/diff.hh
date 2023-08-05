// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <string>
#include <string_view>

std::string diff(std::string_view text_expected, std::string_view text_actual);
