#pragma once

#include <gpiod.hpp>

#include <functional>
#include <thread>
#include <mutex>

namespace Gpio
{
    enum class EdgeType
    {
        NONE,
        RISING,
        FALLING,
        BOTH
    };

    enum class Direction
    {
        INPUT,
        OUTPUT
    };

    enum class Value : bool
    {
        LOW = false,
        HIGH = true
    };

    using InterruptCallback = std::function<void(unsigned int pin, Value value, std::chrono::steady_clock::time_point timestamp)>;

    struct InterruptConfig
    {
        EdgeType edgeType{EdgeType::RISING};
        std::chrono::milliseconds debouncePeriod{10};
        InterruptCallback callback;
        bool enabled{false};
    };

    class Gpio
    {
    public:
        Gpio(const std::string &chipPath, std::pair<uint8_t, uint8_t> input_pin_range, std::pair<uint8_t, uint8_t> output_pin_range);
        ~Gpio();

        Gpio(const Gpio &) = delete;
        Gpio &operator=(const Gpio &) = delete;
        Gpio(Gpio &&) noexcept;
        Gpio &operator=(Gpio &&) noexcept;

        void SetOutput(unsigned int pin, Value value);
        void SetOutputs(const std::vector<std::pair<unsigned int, Value>> &pinValues);
        void SetOutputMask(uint32_t mask, uint32_t values);

        Value GetInput(unsigned int pin);
        std::vector<Value> GetInputs(const std::vector<unsigned int> &pins);
        uint32_t GetInputMask(uint32_t mask = 0xFFFFFFFF);

        Value GetPinValue(unsigned int pin);

        void EnableInterrupt(unsigned int pin, const InterruptConfig &config);
        void DisableInterrupt(unsigned int pin);
        void DisableAllInterrupts();

        bool IsOutputPin(unsigned int pin) const noexcept;
        bool IsInputPin(unsigned int pin) const noexcept;
        bool IsValidPin(unsigned int pin) const noexcept;

    private:
        struct LineInfo
        {
            InterruptConfig config;
            std::chrono::steady_clock::time_point lastEventTime;
        };
        std::string m_ChipPath;
        gpiod::chip m_Chip;
        std::pair<uint8_t, uint8_t> m_InputPinRange;
        std::pair<uint8_t, uint8_t> m_OutputPinRange;

        std::unique_ptr<gpiod::line_request> m_OutputRequest;
        std::unique_ptr<gpiod::line_request> m_InputRequest;
        std::shared_ptr<gpiod::line_request> m_EventRequest;
        std::unordered_map<unsigned int, std::unique_ptr<LineInfo>> m_InterruptLines;

        mutable std::mutex m_GpioMutex;

        std::jthread m_EventThread;
        std::atomic<bool> m_Running{false};

        void InitChip();
        void Cleanup();
        void StartEventMonitoring();
        void StopEventMonitoring();
        void EventMonitorLoop(std::stop_token stopToken);
        void UpdateEventRequest();

        void ValidateOutputPin(unsigned int pin) const;
        void ValidateInputPin(unsigned int pin) const;
        void EnsureOutputRequest();
        void EnsureInputRequest();

        bool ShouldProcessEvent(const LineInfo &lineInfo, std::chrono::steady_clock::time_point eventTime) const;
        gpiod::line::edge ConvertEdgeType(EdgeType edgeType) const;
    };
}