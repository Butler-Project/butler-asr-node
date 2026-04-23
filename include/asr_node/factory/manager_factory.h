#ifndef ASR_NODE_FACTORY_MANAGER_FACTORY_H
#define ASR_NODE_FACTORY_MANAGER_FACTORY_H

#include <memory>

#include "builder/manager_builder.h"
#include "manager/manager.h"

namespace asr::factory {

template <typename ManagerT = ::asr::manager::Manager<>>
struct ManagerFactory {
    using Builder = ::asr::builder::ManagerBuilder<ManagerT>;
    using Config = typename ManagerT::Config;

    [[nodiscard]] static std::unique_ptr<ManagerT> create() {
        return Builder{}.build_unique();
    }

    [[nodiscard]] static std::unique_ptr<ManagerT> create(const Config& config) {
        return std::make_unique<ManagerT>(config);
    }

    [[nodiscard]] static std::unique_ptr<ManagerT> create(const Builder& builder) {
        return builder.build_unique();
    }
};

} // namespace asr::factory

#endif // ASR_NODE_FACTORY_MANAGER_FACTORY_H
