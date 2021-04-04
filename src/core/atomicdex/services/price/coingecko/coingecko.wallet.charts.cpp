
//! Project Headers
#include "atomicdex/api/coingecko/coingecko.hpp"
#include "atomicdex/models/qt.portfolio.model.hpp"
#include "atomicdex/pages/qt.portfolio.page.hpp"
#include "atomicdex/services/price/coingecko/coingecko.wallet.charts.hpp"
#include "atomicdex/services/price/global.provider.hpp"
#include "atomicdex/utilities/qt.utilities.hpp"

namespace
{
    std::string
    get_days_from_wallet_category(WalletChartsCategories category)
    {
        switch (category)
        {
        case atomic_dex::WalletChartsCategoriesGadget::OneDay:
            return "1";
        case atomic_dex::WalletChartsCategoriesGadget::OneWeek:
            return "7";
        case atomic_dex::WalletChartsCategoriesGadget::OneMonth:
            return "30";
        case atomic_dex::WalletChartsCategoriesGadget::Ytd:
            return "";
        case atomic_dex::WalletChartsCategoriesGadget::Size:
            return "";
        }
    }
} // namespace

//! Constructor / Destructor
namespace atomic_dex
{
    coingecko_wallet_charts_service::coingecko_wallet_charts_service(entt::registry& registry, ag::ecs::system_manager& system_manager) :
        system(registry), m_system_manager(system_manager)
    {
        SPDLOG_INFO("coingecko_wallet_charts_service created");
        m_update_clock = std::chrono::high_resolution_clock::now();
    }

    coingecko_wallet_charts_service::~coingecko_wallet_charts_service() { SPDLOG_INFO("coingecko_wallet_charts_service destroyed"); }
} // namespace atomic_dex

//! Private member functions
namespace atomic_dex
{
    void
    coingecko_wallet_charts_service::generate_fiat_chart()
    {
        auto functor = [this]() {
            try
            {
                SPDLOG_INFO("Generate fiat chart");
                const auto fiat = m_system_manager.get_system<settings_page>().get_current_fiat().toStdString();
                t_float_50 rate = safe_float(m_system_manager.get_system<global_price_service>().get_fiat_rates(fiat));
                auto           chart_registry = this->m_chart_data_registry.get();
                nlohmann::json out            = nlohmann::json::array();
                const auto&    data           = chart_registry.begin()->second;
                const auto&    mm2            = m_system_manager.get_system<mm2_service>();
                for (std::size_t idx = 0; idx < data.size(); idx++)
                {
                    nlohmann::json cur = nlohmann::json::object();
                    cur["timestamp"]   = data[idx][0];
                    cur["human_date"]  = utils::to_human_date<std::chrono::milliseconds>(cur.at("timestamp").get<std::size_t>(), "%e %b %Y, %H:%M");
                    t_float_50 total(0);
                    bool       to_skip = false;
                    for (auto&& [key, value]: chart_registry)
                    {
                        if (idx >= value.size())
                        {
                            SPDLOG_ERROR("skipping idx: {}", idx);
                            to_skip = true;
                            continue;
                        }
                        total += (t_float_50(value[idx][1].get<float>()) * mm2.get_balance(key)) * rate;
                    }
                    if (to_skip)
                    {
                        continue;
                    }
                    cur["total"] = utils::format_float(total);
                    out.push_back(cur);
                }
                auto        now                   = std::chrono::system_clock::now();
                std::size_t timestamp             = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                out[out.size() - 1]["timestamp"]  = timestamp;
                out[out.size() - 1]["total"]      = m_system_manager.get_system<portfolio_page>().get_balance_fiat_all().toStdString();
                out[out.size() - 1]["human_date"] = utils::to_human_date<std::chrono::milliseconds>(timestamp, "%e %b %Y, %H:%M");
                SPDLOG_INFO("out: {}", out.dump());
                m_fiat_charts = std::move(out);
            }
            catch (const std::exception& error)
            {
                SPDLOG_ERROR("Exception caught: {}", error.what());
            }
        };
        functor();
        this->m_is_busy = false;
        emit m_system_manager.get_system<portfolio_page>().chartBusyChanged();
        emit m_system_manager.get_system<portfolio_page>().chartsChanged();
    }

    void
    coingecko_wallet_charts_service::fetch_data_of_single_coin(const coin_config& cfg)
    {
        using namespace std::chrono_literals;
        SPDLOG_INFO("fetch charts data of {} {}", cfg.ticker, cfg.coingecko_id);
        std::function<void(WalletChartsCategories, std::string)> market_functor;

        market_functor = [this, cfg, &market_functor](WalletChartsCategories category, std::string days) {
            //! 30 days
            {
                try
                {
                    t_coingecko_market_chart_request request{.id = cfg.coingecko_id, .vs_currency = "usd", .days = days, .interval = "daily"};
                    auto                             resp = atomic_dex::coingecko::api::async_market_charts(std::move(request)).get();
                    std::string                      body = TO_STD_STR(resp.extract_string(true).get());
                    if (resp.status_code() == 200)
                    {
                        m_chart_data_registry->operator[](cfg.ticker) = nlohmann::json::parse(body).at("prices");
                        SPDLOG_INFO("Successfully retrieve chart data for: {} {}", cfg.ticker, cfg.coingecko_id);
                    }
                    else
                    {
                        std::this_thread::sleep_for(1s);
                        market_functor(category, days);
                    }
                }
                catch (const std::exception& error)
                {
                    SPDLOG_ERROR("Caught exception: {} - retrying.", error.what());
                    std::this_thread::sleep_for(1s);
                    market_functor(category, days);
                }
            }
        };
        market_functor(WalletChartsCategories::OneMonth, get_days_from_wallet_category(WalletChartsCategories::OneMonth));
    }

    void
    coingecko_wallet_charts_service::fetch_all_charts_data()
    {
        SPDLOG_INFO("fetch all charts data");
        this->m_is_busy = true;
        emit       m_system_manager.get_system<portfolio_page>().chartBusyChanged();
        const auto coins           = this->m_system_manager.get_system<portfolio_page>().get_global_cfg()->get_enabled_coins();
        auto*      portfolio_model = this->m_system_manager.get_system<portfolio_page>().get_portfolio();
        auto       final_task      = m_taskflow.emplace([this]() { this->generate_fiat_chart(); }).name("Post task");
        for (auto&& [coin, cfg]: coins)
        {
            if (cfg.coingecko_id == "test-coin")
            {
                continue;
            }
            auto res =
                portfolio_model->match(portfolio_model->index(0, 0), portfolio_model::TickerRole, QString::fromStdString(coin), 1, Qt::MatchFlag::MatchExactly);
            // assert(not res.empty());
            if (not res.empty())
            {
                t_float_50 balance = safe_float(portfolio_model->data(res.at(0), portfolio_model::MainCurrencyBalanceRole).toString().toStdString());
                if (balance > 0)
                {
                    final_task.succeed(m_taskflow.emplace([this, cfg = cfg]() { fetch_data_of_single_coin(cfg); }).name(cfg.ticker));
                }
            }
        }
        SPDLOG_INFO("taskflow: {}", m_taskflow.dump());
        m_executor.run(m_taskflow);
    }
} // namespace atomic_dex

//! Public override
namespace atomic_dex
{
    void
    coingecko_wallet_charts_service::update()
    {
        using namespace std::chrono_literals;

        const auto now = std::chrono::high_resolution_clock::now();
        const auto s   = std::chrono::duration_cast<std::chrono::seconds>(now - m_update_clock);
        if (s >= 1h)
        {
            {
                SPDLOG_INFO("Waiting for previous call to be finished");
                m_executor.wait_for_all();
                m_taskflow.clear();
                m_chart_data_registry->clear();
            }
            fetch_all_charts_data();
            m_update_clock = std::chrono::high_resolution_clock::now();
        }
    }
} // namespace atomic_dex

//! Public member functions
namespace atomic_dex
{
    void
    coingecko_wallet_charts_service::manual_refresh()
    {
        if (m_is_busy)
        {
            SPDLOG_WARN("Service is busy, try later");
            return;
        }
        auto functor = [this]() {
            try
            {
                {
                    SPDLOG_INFO("Waiting for previous call to be finished");
                    m_executor.wait_for_all();
                    m_taskflow.clear();
                    m_chart_data_registry->clear();
                }
                fetch_all_charts_data();
                m_update_clock = std::chrono::high_resolution_clock::now();
            }
            catch (const std::exception& error)
            {
                SPDLOG_ERROR("Exception caught: {}", error.what());
            }
        };
        //[[maybe_unused]] auto res = std::async(functor);
        functor();
    }

    bool
    coingecko_wallet_charts_service::is_busy() const
    {
        return m_is_busy.load();
    }

    QVariant
    coingecko_wallet_charts_service::get_charts() const
    {
        return atomic_dex::nlohmann_json_array_to_qt_json_array(m_fiat_charts.get());
    }
} // namespace atomic_dex