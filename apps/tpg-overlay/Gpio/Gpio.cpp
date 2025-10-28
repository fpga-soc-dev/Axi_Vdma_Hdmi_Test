#include "Gpio.hpp"

namespace Gpio
{
    Gpio::Gpio(const std::string &chip_name, std::pair<uint8_t, uint8_t> input_pin_range, std::pair<uint8_t, uint8_t> output_pin_range)
        : m_ChipPath(chip_name),
          m_Chip(chip_name),
          m_InputPinRange(input_pin_range),
          m_OutputPinRange(output_pin_range)
    {
        InitChip();
    }

    Gpio::~Gpio()
    {
        StopEventMonitoring();
        Cleanup();
    }

    Gpio::Gpio(Gpio &&other) noexcept
        : m_ChipPath(std::move(other.m_ChipPath)),
          m_Chip(std::move(other.m_Chip)),
          m_OutputRequest(std::move(other.m_OutputRequest)),
          m_InputRequest(std::move(other.m_InputRequest)),
          m_EventRequest(std::move(other.m_EventRequest)),
          m_InterruptLines(std::move(other.m_InterruptLines)),
          m_EventThread(std::move(other.m_EventThread)),
          m_Running(other.m_Running.load())
    {
        other.m_Running.store(false);
        other.m_OutputRequest.reset();
        other.m_InputRequest.reset();
        other.m_EventRequest.reset();
        other.m_InterruptLines.clear();
    }

    Gpio &Gpio::operator=(Gpio &&other) noexcept
    {
        if (this != &other)
        {
            StopEventMonitoring();
            Cleanup();

            m_ChipPath = std::move(other.m_ChipPath);
            m_Chip = std::move(other.m_Chip);
            m_OutputRequest = std::move(other.m_OutputRequest);
            m_InputRequest = std::move(other.m_InputRequest);
            m_EventRequest = std::move(other.m_EventRequest);
            m_InterruptLines = std::move(other.m_InterruptLines);
            m_EventThread = std::move(other.m_EventThread);
            m_Running.store(other.m_Running.load());

            other.m_Running.store(false);
            other.m_OutputRequest.reset();
            other.m_InputRequest.reset();
            other.m_EventRequest.reset();
            other.m_InterruptLines.clear();
        }
        return *this;
    }

    void Gpio::InitChip()
    {
        try
        {
            if (m_Chip.get_info().num_lines() < (m_InputPinRange.second + m_OutputPinRange.second))
            {
                throw std::runtime_error("GPIO chip has fewer number of lines" + std::to_string(m_Chip.get_info().num_lines()) +
                                         " than required " + std::to_string(m_InputPinRange.second + m_OutputPinRange.second));
            }
        }
        catch (const std::system_error &e)
        {
            throw std::runtime_error("Failed to open GPIO chip: " + std::string(e.what()));
        }
    }

    void Gpio::Cleanup()
    {
        std::lock_guard<std::mutex> lock(m_GpioMutex);

        m_InterruptLines.clear();
        m_EventRequest.reset();
        m_InputRequest.reset();
        m_OutputRequest.reset();
    }

    void Gpio::StartEventMonitoring()
    {
        if (m_Running.load())
        {
            return;
        }

        m_Running.store(true);
        m_EventThread = std::jthread([this](std::stop_token stopToken)
                                     { EventMonitorLoop(stopToken); });
    }

    void Gpio::StopEventMonitoring()
    {
        if (!m_Running.load())
        {
            return;
        }

        m_Running.store(false);
        if (m_EventThread.joinable())
        {
            m_EventThread.request_stop();
            m_EventThread.join();
        }
    }

    void Gpio::EventMonitorLoop(std::stop_token stopToken)
    {
        constexpr auto timeout = std::chrono::milliseconds(100);

        while (m_Running.load() && !stopToken.stop_requested())
        {
            try
            {
                std::shared_ptr<gpiod::line_request> event_request;
                {
                    std::lock_guard<std::mutex> lock(m_GpioMutex);
                    if (!m_EventRequest || m_InterruptLines.empty())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    event_request = m_EventRequest;
                }

                // wait for interrupts from kernel, with timeout to check m_Running periodically
                if (event_request && event_request->wait_edge_events(timeout))
                {
                    // Creates a buffer to hold up to 16 events at a time
                    gpiod::edge_event_buffer buffer(16);
                    // Read all available events into the buffer
                    auto num_events = event_request->read_edge_events(buffer);
                    for (size_t i = 0; i < num_events; ++i)
                    {
                        // Process each event
                        const auto &event = buffer.get_event(i);
                        // record the time of the event to handle debounce
                        auto event_time = std::chrono::steady_clock::now();
                        // Get the pin number that triggered the event
                        auto pin = event.line_offset();

                        // Protects access to m_InterruptLines. Multi-threaded access possible m_InterruptLines simultaneously.
                        std::lock_guard<std::mutex> interrupt_lock(m_GpioMutex);
                        // Find the corresponding interrupt configuration
                        auto it = m_InterruptLines.find(pin);

                        // If the pin is found and interrupt enabled, process the event
                        if (it != m_InterruptLines.end() &&
                            it->second->config.enabled &&
                            ShouldProcessEvent(*it->second, event_time)) // Check for debounce
                        {
                            // Update the last event time for debounce tracking
                            it->second->lastEventTime = event_time;
                            // Call the user-defined callback function
                            auto callback = it->second->config.callback;
                            // Determine the event value (RISING or FALLING)
                            Value value = static_cast<Value>(event.type() == gpiod::edge_event::event_type::RISING_EDGE);
                            if (callback)
                            {
                                callback(pin, value, event_time);
                            }
                        }
                    }
                }
            }
            catch (const std::system_error &e)
            {
                throw std::runtime_error("Event monitoring thread error: " + std::string(e.what()));
            }
        }
    }

    void Gpio::SetOutput(unsigned int pin, Value value)
    {
        ValidateOutputPin(pin);

        std::unique_lock<std::mutex> lock(m_GpioMutex);
        EnsureOutputRequest();

        try
        {
            gpiod::line::value gpioValue = (value == Value::HIGH) ? gpiod::line::value::ACTIVE : gpiod::line::value::INACTIVE;
            m_OutputRequest->set_value(pin, gpioValue);
        }
        catch (const std::system_error &e)
        {
            throw std::runtime_error("Failed to set GPIO output: " + std::string(e.what()));
        }
    }

    void Gpio::SetOutputs(const std::vector<std::pair<unsigned int, Value>> &pinValues)
    {
        std::lock_guard<std::mutex> lock(m_GpioMutex);
        EnsureOutputRequest();
        std::vector<gpiod::line::offset> offsets;
        std::vector<gpiod::line::value> values;

        for (const auto &[pin, value] : pinValues)
        {
            ValidateOutputPin(pin);
            offsets.push_back(pin);
            gpiod::line::value gpioValue = (value == Value::HIGH) ? gpiod::line::value::ACTIVE : gpiod::line::value::INACTIVE;
            values.push_back(gpioValue);
        }

        try
        {
            m_OutputRequest->set_values(offsets, values);
        }
        catch (const std::system_error &e)
        {
            throw std::runtime_error("Failed to set GPIO outputs: " + std::string(e.what()));
        }
    }

    void Gpio::SetOutputMask(uint32_t mask, uint32_t values)
    {
        std::lock_guard<std::mutex> lock(m_GpioMutex);
        EnsureOutputRequest();

        std::vector<gpiod::line::offset> offsets;
        std::vector<gpiod::line::value> gpio_values;

        for (unsigned int i = m_OutputPinRange.first; i <= m_OutputPinRange.second; ++i)
        {
            if (mask & (1u << i))
            {
                offsets.push_back(i);
                gpiod::line::value value = (values & (1u << i)) ? gpiod::line::value::ACTIVE : gpiod::line::value::INACTIVE;
                gpio_values.push_back(value);
            }
        }

        try
        {
            m_OutputRequest->set_values(offsets, gpio_values);
        }
        catch (const std::system_error &e)
        {
            throw std::runtime_error("Failed to set GPIO output mask: " + std::string(e.what()));
        }
    }

    Value Gpio::GetInput(unsigned int pin)
    {
        ValidateInputPin(pin);

        std::lock_guard<std::mutex> lock(m_GpioMutex);
        EnsureInputRequest();

        try
        {
            auto value = m_InputRequest->get_value(pin);
            return (value == gpiod::line::value::ACTIVE) ? Value::HIGH : Value::LOW;
        }
        catch (const std::system_error &e)
        {
            throw std::runtime_error("Failed to get GPIO input: " + std::string(e.what()));
        }
    }

    std::vector<Value> Gpio::GetInputs(const std::vector<unsigned int> &pins)
    {
        std::lock_guard<std::mutex> lock(m_GpioMutex);
        EnsureInputRequest();

        std::vector<gpiod::line::offset> offsets;
        for (unsigned int pin : pins)
        {
            ValidateInputPin(pin);
            offsets.push_back(pin);
        }

        try
        {
            auto values = m_InputRequest->get_values(offsets);
            std::vector<Value> results;
            results.reserve(values.size());

            for (const auto &value : values)
            {
                results.emplace_back(static_cast<Value>(value == gpiod::line::value::ACTIVE));
            }

            return results;
        }
        catch (const std::system_error &e)
        {
            throw std::runtime_error("Failed to get GPIO inputs: " + std::string(e.what()));
        }
    }

    uint32_t Gpio::GetInputMask(uint32_t mask)
    {
        std::lock_guard<std::mutex> lock(m_GpioMutex);
        EnsureInputRequest();

        std::vector<gpiod::line::offset> offsets;
        std::vector<unsigned int> bit_positions;

        for (unsigned int i = m_InputPinRange.first; i <= m_InputPinRange.second; ++i)
        {
            unsigned int bit_pos = i - m_InputPinRange.first;
            if (mask & (1U << bit_pos))
            {
                offsets.push_back(i);
                bit_positions.push_back(bit_pos);
            }
        }

        try
        {
            if (offsets.empty())
            {
                return 0;
            }

            auto values = m_InputRequest->get_values(offsets);
            uint32_t result = 0;

            for (size_t i = 0; i < values.size(); ++i)
            {
                if (values[i] == gpiod::line::value::ACTIVE)
                {
                    result |= (1U << bit_positions[i]);
                }
            }

            return result;
        }
        catch (const std::system_error &e)
        {
            throw std::runtime_error("Failed to get GPIO input mask: " + std::string(e.what()));
        }
    }

    Value Gpio::GetPinValue(unsigned int pin)
    {
        std::lock_guard<std::mutex> lock(m_GpioMutex);

        if (IsOutputPin(pin))
        {
            EnsureOutputRequest();
            try
            {
                auto value = m_OutputRequest->get_value(pin);
                return static_cast<Value>(value);
            }
            catch (const std::system_error &e)
            {
                throw std::runtime_error("Failed to get output value for pin " + std::to_string(pin) + ": " + e.what());
            }
        }
        else if (IsInputPin(pin))
        {
            EnsureInputRequest();
            try
            {
                auto value = m_InputRequest->get_value(pin);
                return static_cast<Value>(value);
            }
            catch (const std::system_error &e)
            {
                throw std::runtime_error("Failed to get input value for pin " + std::to_string(pin) + ": " + e.what());
            }
        }
        else
        {
            throw std::out_of_range("Pin " + std::to_string(pin) + " is not a valid GPIO pin.");
        }
    }

    void Gpio::EnableInterrupt(unsigned int pin, const InterruptConfig &config)
    {
        ValidateInputPin(pin);

        if (!config.callback)
        {
            throw std::invalid_argument("Interrupt callback must be provided.");
        }

        std::lock_guard<std::mutex> lock(m_GpioMutex);

        auto lineInfo = std::make_unique<LineInfo>();
        lineInfo->config = config;
        lineInfo->lastEventTime = std::chrono::steady_clock::now();

        m_InterruptLines[pin] = std::move(lineInfo);

        UpdateEventRequest();

        if (!m_Running.load())
        {
            StartEventMonitoring();
        }
    }

    void Gpio::DisableInterrupt(unsigned int pin)
    {
        ValidateInputPin(pin);

        std::lock_guard<std::mutex> lock(m_GpioMutex);
        auto it = m_InterruptLines.find(pin);
        if (it != m_InterruptLines.end())
        {

            m_InterruptLines.erase(it);

            try
            {
                UpdateEventRequest();
            }
            catch (const std::exception &e)
            {
                std::cerr << "Warning: Failed to update event request after disabling pin "
                          << pin << ": " << e.what() << std::endl;

                m_EventRequest.reset();
            }
        }
    }

    void Gpio::DisableAllInterrupts()
    {
        std::lock_guard<std::mutex> lock(m_GpioMutex);
        m_InterruptLines.clear();
        m_EventRequest.reset();
    }

    void Gpio::ValidateOutputPin(unsigned int pin) const
    {
        if (pin < m_OutputPinRange.first || pin > m_OutputPinRange.second)
        {
            throw std::out_of_range("Pin " + std::to_string(pin) + " is not a valid output pin.");
        }
    }

    void Gpio::ValidateInputPin(unsigned int pin) const
    {
        if (pin < m_InputPinRange.first || pin > m_InputPinRange.second)
        {
            throw std::out_of_range("Pin " + std::to_string(pin) + " is not a valid input pin.");
        }
    }

    void Gpio::UpdateEventRequest()
    {
        m_EventRequest.reset();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (m_InterruptLines.empty())
        {
            return;
        }

        try
        {
            // Create a vector of offsets for lines that need pin monitoring
            std::vector<gpiod::line::offset> event_offsets;
            // Hardware configuration for event monitoring
            gpiod::line_config event_config;

            for (const auto &[pin, line_info] : m_InterruptLines)
            {
                // Only add lines that are enabled and have a valid edge type
                if (line_info->config.enabled && line_info->config.edgeType != EdgeType::NONE)
                {
                    // Add the pin to the list of offsets to monitor
                    event_offsets.push_back(pin);
                    // Configure line settings for the pin
                    gpiod::line_settings event_settings;
                    // Set the line as input for event detection
                    event_settings.set_direction(gpiod::line::direction::INPUT);
                    // Set the edge detection type based on user configuration
                    event_settings.set_edge_detection(ConvertEdgeType(line_info->config.edgeType));
                    // Add the configured settings for this pin to the overall event configuration
                    event_config.add_line_settings(pin, event_settings);
                }
            }
            // If there are any lines to monitor, create the event request
            if (!event_offsets.empty())
            {
                // Use chip's prepare_request method (correct for libgpiod 2.x)
                auto request_builder = m_Chip.prepare_request();
                // Set a consumer label for identification
                request_builder.set_consumer("AXI_GPIO_Events");
                // Apply the event line configuration to the request
                request_builder.set_line_config(event_config);
                // Execute the request to obtain a line_request object
                // Kernel allocate GPIO lines to reserve pins for event monitoring
                // Hardware interrupt setup, GPIO controller configured for edge detection
                // No other program can use these lines until released. If in use, throws exception
                auto line_request = request_builder.do_request();
                // Store the line_request in a shared_ptr for event monitoring
                m_EventRequest = std::make_shared<gpiod::line_request>(std::move(line_request));
            }
        }
        catch (const std::system_error &e)
        {
            std::cerr << "Failed to update event request: " << e.what() << std::endl;
            m_EventRequest.reset();
        }
    }

    void Gpio::EnsureOutputRequest()
    {
        if (!m_OutputRequest)
        {
            try
            {
                // Create list of output pins
                std::vector<gpiod::line::offset> output_offsets;
                output_offsets.reserve(m_OutputPinRange.second - m_OutputPinRange.first + 1);
                for (unsigned int i = m_OutputPinRange.first; i <= m_OutputPinRange.second; ++i)
                {
                    output_offsets.push_back(static_cast<gpiod::line::offset>(i));
                }

                // Create line settings for outputs
                gpiod::line_settings output_settings;
                // Set direction to output and initial value to low
                output_settings.set_direction(gpiod::line::direction::OUTPUT);
                output_settings.set_output_value(gpiod::line::value::INACTIVE);

                // Apply same settings to all output pins
                gpiod::line_config output_config;
                output_config.add_line_settings(output_offsets, output_settings);

                // Creates request builder for GPIO chip
                auto request_builder = m_Chip.prepare_request();
                // Resource tracking label in kernel GPIO debugfs
                request_builder.set_consumer("AXI_GPIO_Output");
                // Tells kernel how to configure pins, as a bulk configuration
                request_builder.set_line_config(output_config);
                // Kernel interaction, syscall to GPIO subsystem
                // Hardware configuration for output pins
                // Pins are exclusively reserved for this process
                auto line_request = request_builder.do_request();
                m_OutputRequest = std::make_unique<gpiod::line_request>(std::move(line_request));
            }
            catch (const std::system_error &e)
            {
                throw std::runtime_error("Failed to create output request: " + std::string(e.what()));
            }
        }
    }

    void Gpio::EnsureInputRequest()
    {
        if (!m_InputRequest)
        {
            try
            {

                std::vector<gpiod::line::offset> input_offsets;
                input_offsets.reserve(m_InputPinRange.second - m_InputPinRange.first + 1);
                for (unsigned int i = m_InputPinRange.first; i <= m_InputPinRange.second; ++i)
                {
                    input_offsets.push_back(static_cast<gpiod::line::offset>(i));
                }

                gpiod::line_settings input_settings;
                input_settings.set_direction(gpiod::line::direction::INPUT);

                gpiod::line_config input_config;
                input_config.add_line_settings(input_offsets, input_settings);

                auto request_builder = m_Chip.prepare_request();
                request_builder.set_consumer("AXI_GPIO_Input");
                request_builder.set_line_config(input_config);

                auto line_request = request_builder.do_request();
                m_InputRequest = std::make_unique<gpiod::line_request>(std::move(line_request));
            }
            catch (const std::system_error &e)
            {
                throw std::runtime_error("Failed to create input request: " + std::string(e.what()));
            }
        }
    }

    bool Gpio::ShouldProcessEvent(const LineInfo &lineInfo, std::chrono::steady_clock::time_point eventTime) const
    {
        // Button Press Signal (without debouncing):
        // Time:  0ms    5ms   10ms   15ms   20ms   25ms   30ms
        // Signal:LOW → HIGH → LOW → HIGH → LOW → HIGH → HIGH
        //        ↑     ↑      ↑     ↑      ↑     ↑      ↑
        //        |     |      |     |      |     |      |
        //      Press Bounce Bounce Bounce Bounce Bounce Stable
        // Without debouncing: 6 interrupts generated for 1 button press!
        // With debouncing: Only 1 interrupt processed

        // const LineInfo &lineInfo        - Contains interrupt config + last event timestamp
        // std::chrono::steady_clock::time_point eventTime  - When current event occurred
        auto timeSinceLastEvent = eventTime - lineInfo.lastEventTime;
        // Threshold time - has enough time passed?
        // each pin can have different debounce periods
        // Boolean result: true = process event, false = ignore event
        return timeSinceLastEvent >= lineInfo.config.debouncePeriod;
    }

    gpiod::line::edge Gpio::ConvertEdgeType(EdgeType edgeType) const
    {
        switch (edgeType)
        {
        case EdgeType::RISING:
            return gpiod::line::edge::RISING;
        case EdgeType::FALLING:
            return gpiod::line::edge::FALLING;
        case EdgeType::BOTH:
            return gpiod::line::edge::BOTH;
        case EdgeType::NONE:
        default:
            return gpiod::line::edge::NONE;
        }
    }

    bool Gpio::IsOutputPin(unsigned int pin) const noexcept
    {
        return pin >= m_OutputPinRange.first && pin <= m_OutputPinRange.second;
    }

    bool Gpio::IsInputPin(unsigned int pin) const noexcept
    {
        return pin >= m_InputPinRange.first && pin <= m_InputPinRange.second;
    }

    bool Gpio::IsValidPin(unsigned int pin) const noexcept
    {
        return pin < m_InputPinRange.second + m_OutputPinRange.second;
    }
}