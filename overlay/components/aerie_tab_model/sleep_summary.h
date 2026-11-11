// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_SLEEP_SUMMARY_H_
#define COMPONENTS_AERIE_TAB_MODEL_SLEEP_SUMMARY_H_

#include <stdint.h>

namespace aerie_tab_model {

// The memory that sleeping tabs freed, for the Today header (issue #179): the
// number of megabytes (1 MB = 1048576 bytes) rounded to the nearest 10. 0 means
// there is no estimate to show (under 5 MB, or no value).
int RoundMemoryEstimateMb(int64_t bytes);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_SLEEP_SUMMARY_H_
