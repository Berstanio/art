/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_SIGCHAINLIB_SIGCHAIN_H_
#define ART_SIGCHAINLIB_SIGCHAIN_H_

#include <signal.h>

namespace art {

extern "C" void InitializeSignalChain();

extern "C" void ClaimSignalChain(int signal, struct sigaction* oldaction);

extern "C" void UnclaimSignalChain(int signal);

typedef bool (*SpecialSignalHandlerFn)(int, siginfo_t*, void*);
extern "C" void SetSpecialSignalHandlerFn(int signal, SpecialSignalHandlerFn fn);

#ifndef MOE
extern "C" void InvokeUserSignalHandler(int sig, siginfo_t* info, void* context);
#else
extern "C" bool InvokeUserSignalHandler(int sig, siginfo_t* info, void* context);
#endif

extern "C" void EnsureFrontOfChain(int signal, struct sigaction* expected_action);

}  // namespace art

#endif  // ART_SIGCHAINLIB_SIGCHAIN_H_
