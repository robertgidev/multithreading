#pragma once

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

// Prints the `text` to stdout, appends '\n' and flushes the stream in a thread-safe manner.
// Should not be used by the final logger implementation. Useful for debugging.
void print(std::string_view text);

// Helper function, that reverses a singly linked list.
template <class T>
constexpr T* reverse(T* head) noexcept {
  T* list = nullptr;
  while (head) {
    const auto next = head->next;
    head->next = list;
    list = head;
    head = next;
  }
  return list;
}

class logger {
public:
  logger() = default;
  logger(const logger&) = delete;
  logger& operator=(const logger&) = delete;

  ~logger() {
    destroy(head_.exchange(nullptr, std::memory_order_acquire));
  }

  void post(std::string text) {
    auto* item = new node{std::move(text)};

    auto* old_head = head_.load(std::memory_order_relaxed);
    do {
      item->next = old_head;
    } while (!head_.compare_exchange_weak(old_head, item,
                                          std::memory_order_release,
                                          std::memory_order_relaxed));

    if (item->next == nullptr) {
      wakeups_.fetch_add(1, std::memory_order_relaxed);
      wakeups_.notify_one();
    }
  }

  void run(std::stop_token stop) {
    std::stop_callback wake_on_stop{stop, [this] {
      wakeups_.fetch_add(1, std::memory_order_relaxed);
      wakeups_.notify_one();
    }};

    for (;;) {
      while (drain_once()) {
      }

      if (stop.stop_requested()) {
        while (drain_once()) {
        }
        return;
      }

      const auto seen = wakeups_.load(std::memory_order_relaxed);
      if (head_.load(std::memory_order_acquire) == nullptr && !stop.stop_requested()) {
        wakeups_.wait(seen, std::memory_order_relaxed);
      }
    }
  }

private:
  struct node {
    explicit node(std::string value) : text(std::move(value)) {}

    std::string text;
    node* next = nullptr;
  };

  bool drain_once() {
    auto* list = head_.exchange(nullptr, std::memory_order_acquire);
    if (list == nullptr)
      return false;

    list = reverse(list);
    while (list != nullptr) {
      auto* next = list->next;
      std::fwrite(list->text.data(), 1, list->text.size(), stdout);
      delete list;
      list = next;
    }
    std::fflush(stdout);
    return true;
  }

  static void destroy(node* list) noexcept {
    while (list != nullptr) {
      auto* next = list->next;
      delete list;
      list = next;
    }
  }

  std::atomic<node*> head_{nullptr};
  std::atomic<std::uint64_t> wakeups_{0};
};

#define logger logger
