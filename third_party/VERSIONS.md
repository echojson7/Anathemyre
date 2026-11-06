# Сторонние библиотеки в репозитории

| Библиотека | Версия (тег) | Коммит | Лицензия | Что взято |
|---|---|---|---|---|
| Vulkan-Headers | v1.4.367 | 9fbc2a782ee3f8a4db64e7d991e23da75e893f44 | Apache-2.0 или MIT | `include/vulkan/*.h`, `include/vk_video/` (без C++-обёрток) |
| volk | vulkan-sdk-1.4.363.0 | 54fc0d7a72887cbfc5b030d26897b2b40ac53fb8 | MIT | `volk.c`, `volk.h` |
| VulkanMemoryAllocator | v3.5.0 | 0f1a757da579831283413295cc7f3b47eb521c77 | MIT | `vk_mem_alloc.h` |
| vk-bootstrap | v1.4.367 | 294dc2cf9928d7ffce7ded39125d964b4f0362a0 | MIT | `src/*` |

`vma_impl.cpp` — наш файл: реализация VMA с функциями от volk.
SDL3 (zlib) здесь не лежит: версия и sha256 в `tools/versions.sh`, собирает `tools/third_party.sh` (`docs/build.md`).
Обновление: заменить файлы целиком, поправить строку здесь и запись в `docs/decisions.md`.
