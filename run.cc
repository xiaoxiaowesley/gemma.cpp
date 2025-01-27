// Copyright 2024 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Command line text interface to gemma.

#include <ctime>
#include <iostream>
#include <random>
#include <string>
#include <thread>  // NOLINT
#include <vector>

// copybara:import_next_line:gemma_cpp
#include "compression/compress.h"
// copybara:import_next_line:gemma_cpp
#include "gemma.h"    // Gemma
// copybara:import_next_line:gemma_cpp
#include "util/app.h"
// copybara:import_next_line:gemma_cpp
#include "util/args.h"  // HasHelp
#include "hwy/base.h"
#include "hwy/contrib/thread_pool/thread_pool.h"
#include "hwy/highway.h"
#include "hwy/per_target.h"
#include "hwy/profiler.h"
#include "hwy/timer.h"

namespace gcpp {

void ShowHelp(gcpp::LoaderArgs& loader, gcpp::InferenceArgs& inference,
              gcpp::AppArgs& app) {
  fprintf(stderr,
          "\ngemma.cpp\n---------\n\nTo run gemma.cpp, you need to "
          "specify 3 required model loading arguments: --tokenizer, "
          "--compressed_weights, "
          "and --model.\n\nModel Loading Arguments\n\n");
  loader.Help();
  fprintf(stderr, "\nInference Arguments\n\n");
  inference.Help();
  fprintf(stderr, "\nApplication Arguments\n\n");
  app.Help();
  fprintf(stderr, "\n\n");
}

void ShowConfig(LoaderArgs& loader, InferenceArgs& inference, AppArgs& app) {
  loader.Print(app.verbosity);
  inference.Print(app.verbosity);
  app.Print(app.verbosity);

  if (app.verbosity >= 2) {
    time_t now = time(nullptr);
    char* dt = ctime(&now);  // NOLINT
    std::cout << "Date & Time                   : " << dt
              << "Prefill Token Batch Size      : " << gcpp::kPrefillBatchSize
              << "\n"
              << "Hardware concurrency          : "
              << std::thread::hardware_concurrency() << std::endl
              << "Instruction set               : "
              << hwy::TargetName(hwy::DispatchedTarget()) << " ("
              << hwy::VectorBytes() * 8 << " bits)" << "\n"
              << "Weight Type                   : "
              << gcpp::TypeName(gcpp::WeightT()) << "\n"
              << "EmbedderInput Type            : "
              << gcpp::TypeName(gcpp::EmbedderInputT()) << "\n";
  }
}

/**
 * @brief 执行Gemma REPL循环。(read-eval-print loop)
 * 
 * @param model Gemma模型对象。
 * @param pool 线程池对象。
 * @param inner_pool 内部线程池对象。
 * @param args 推理参数对象。
 * @param verbosity 详细程度。
 * @param accept_token 接受令牌的函数。
 */
void ReplGemma(gcpp::Gemma& model, hwy::ThreadPool& pool,
               hwy::ThreadPool& inner_pool, const InferenceArgs& args,
               int verbosity, const gcpp::AcceptFunc& accept_token) {
  PROFILER_ZONE("Gen.misc");
  int abs_pos = 0;      // absolute token index over all turns
  int current_pos = 0;  // token index within the current turn
  int prompt_size{};

  std::mt19937 gen;
  if (args.deterministic) {
    gen.seed(42);
  } else {
    std::random_device rd;
    gen.seed(rd());
  }

  // callback function invoked for each generated token.
  auto stream_token = [&abs_pos, &current_pos, &args, &gen, &prompt_size,
                       tokenizer = &model.Tokenizer(),
                       verbosity](int token, float) {
    ++abs_pos;
    ++current_pos;
    if (current_pos < prompt_size) {
      std::cerr << "." << std::flush;
    } else if (token == gcpp::EOS_ID) {
      if (!args.multiturn) {
        abs_pos = 0;
        if (args.deterministic) {
          gen.seed(42);
        }
      }
      if (verbosity >= 2) {
        std::cout << "\n[ End ]" << std::endl;
      }
    } else {
      std::string token_text;
      HWY_ASSERT(tokenizer->Decode(std::vector<int>{token}, &token_text).ok());
      // +1 since position is incremented above
      if (current_pos == prompt_size + 1) {
        // first token of response
        token_text.erase(0, token_text.find_first_not_of(" \t\n"));
        if (verbosity >= 1) {
          std::cout << std::endl << std::endl;
        }
      }
      // TODO(austinvhuang): is explicit space necessary?
      std::cout << token_text << std::flush;
    }
    return true;
  };

  while (abs_pos < args.max_tokens) {
    std::string prompt_string;
    std::vector<int> prompt;
    current_pos = 0;
    {
      PROFILER_ZONE("Gen.input");
      if (verbosity >= 1) {
        std::cout << "> " << std::flush;
      }
      std::getline(std::cin, prompt_string);
    }

    if (std::cin.fail() || prompt_string == "%q" || prompt_string == "%Q") {
      return;
    }

    if (prompt_string == "%c" || prompt_string == "%C") {
      abs_pos = 0;
      continue;
    }

    if (model.model_training == ModelTraining::GEMMA_IT) {
      // For instruction-tuned models: add control tokens.
      prompt_string = "<start_of_turn>user\n" + prompt_string +
                      "<end_of_turn>\n<start_of_turn>model\n";
      if (abs_pos > 0) {
        // Prepend "<end_of_turn>" token if this is a multi-turn dialogue
        // continuation.
        prompt_string = "<end_of_turn>\n" + prompt_string;
      }
    }

    HWY_ASSERT(model.Tokenizer().Encode(prompt_string, &prompt).ok());

    // For both pre-trained and instruction-tuned models: prepend "<bos>" token
    // if needed.
    if (abs_pos == 0) {
      prompt.insert(prompt.begin(), 2);
    }

    prompt_size = prompt.size();

    std::cerr << std::endl << "[ Reading prompt ] " << std::flush;

    const double time_start = hwy::platform::Now();
    GenerateGemma(model, args, prompt, abs_pos, pool, inner_pool, stream_token,
                  accept_token, gen, verbosity);
    const double time_end = hwy::platform::Now();
    const double tok_sec = current_pos / (time_end - time_start);
    if (verbosity >= 2) {
      std::cout << current_pos << " tokens (" << abs_pos << " total tokens)"
                << std::endl
                << tok_sec << " tokens / sec" << std::endl;
    }
    std::cout << std::endl << std::endl;
  }
  std::cout
      << "max_tokens (" << args.max_tokens
      << ") exceeded. Use a larger value if desired using the --max_tokens "
      << "command line flag.\n";
}

void Run(LoaderArgs& loader, InferenceArgs& inference, AppArgs& app) {
  PROFILER_ZONE("Run.misc");

  hwy::ThreadPool inner_pool(0);
  hwy::ThreadPool pool(app.num_threads);
  // For many-core, pinning threads to cores helps.
  if (app.num_threads > 10) {
    PinThreadToCore(app.num_threads - 1);  // Main thread

    pool.Run(0, pool.NumThreads(),
             [](uint64_t /*task*/, size_t thread) { PinThreadToCore(thread); });
  }

  gcpp::Gemma model(loader, pool);

  if (const char* error = inference.Validate()) {
    ShowHelp(loader, inference, app);
    HWY_ABORT("\nInvalid args: %s", error);
  }

  if (app.verbosity >= 1) {
    static const std::string banner_ascii_art =
        "  __ _  ___ _ __ ___  _ __ ___   __ _   ___ _ __  _ __\n"
        " / _` |/ _ \\ '_ ` _ \\| '_ ` _ \\ / _` | / __| '_ \\| '_ \\\n"
        "| (_| |  __/ | | | | | | | | | | (_| || (__| |_) | |_) |\n"
        " \\__, |\\___|_| |_| |_|_| |_| |_|\\__,_(_)___| .__/| .__/\n"
        "  __/ |                                    | |   | |\n"
        " |___/                                     |_|   |_|";

    const std::string instructions =
        "*Usage*\n"
        "  Enter an instruction and press enter (%Q quits).\n\n"
        "*Examples*\n"
        "  - Write an email to grandma thanking her for the cookies.\n"
        "  - What are some historical attractions to visit around "
        "Massachusetts?\n"
        "  - Compute the nth fibonacci number in javascript.\n"
        "  - Write a standup comedy bit about GPU programming.\n";

    std::cout << "\033[2J\033[1;1H"  // clear screen
              << banner_ascii_art << "\n\n";
    ShowConfig(loader, inference, app);
    std::cout << "\n" << instructions << "\n";
  }

  ReplGemma(model, pool, inner_pool, inference, app.verbosity,
            /*accept_token=*/[](int) { return true; });
}

}  // namespace gcpp

int main(int argc, char** argv) {
  {
    PROFILER_ZONE("Startup.misc");

    gcpp::LoaderArgs loader(argc, argv);
    gcpp::InferenceArgs inference(argc, argv);
    gcpp::AppArgs app(argc, argv);

    if (gcpp::HasHelp(argc, argv)) {
      ShowHelp(loader, inference, app);
      return 0;
    }

    if (const char* error = loader.Validate()) {
      ShowHelp(loader, inference, app);
      HWY_ABORT("\nInvalid args: %s", error);
    }

    gcpp::Run(loader, inference, app);
  }
  PROFILER_PRINT_RESULTS();  // Must call outside the zone above.
  return 0;
}
