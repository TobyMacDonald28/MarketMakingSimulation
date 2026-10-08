Multithreaded C++ / Python Trading Engine
A high-performance, deterministic Limit Order Book (LOB) matching engine built in modern C++17, seamlessly integrated with a Python machine learning backend via Pybind11.

Achieves ~7.5M Operations Per Second (OPS) with 84ns median and <500ns p99 latency on a single-threaded mixed market-maker workload, while guaranteeing ACID-like financial consistency across concurrent trading bots.



🚀 Key Features
Core Matching Engine: Deterministic, multithreaded LOB utilizing C++ STL containers (std::map, std::list, std::unordered_map) to rigorously enforce strict price-time priority.

Thread-Safe Concurrency: Publisher/Subscriber architecture safely managing concurrent market participants (Market Makers, Momentum Traders, ML Snipers) using std::mutex and std::atomic to eliminate race conditions and cross-thread deadlocks.

Embedded Python FFI: Leverages a Pybind11 Foreign Function Interface (FFI) with an embedded CPython interpreter to dynamically instantiate and execute Python-based machine learning modules directly within the low-latency C++ core.

Predictive ML Backend: Python strategy backend evaluating Order Book Imbalance (OBI) and spread width to predict micro-price movements and generate trading signals, allowing rapid iteration without recompiling the core.

Verifiable Simulation: Tick-based deterministic execution loop for tick-by-tick replay, robust unit testing, and algorithmic debugging.



📁 Project Structure
Here is how the project is organized:

Plaintext
TradingGame/
├── CMakeLists.txt           # Master build instructions
├── .gitignore               # Git ignore rules for build/ and venv/
├── requirements.txt         # Python dependencies (pandas, scikit-learn, etc.)
├── cpp_engine/              # Core C++ Execution Engine
│   ├── main.cpp             # Entry point; thread spawning
│   ├── order_book.hpp       # LOB class declarations
│   ├── order_book.cpp       # LOB logic and match execution
│   ├── traders.hpp          # Trader bot interfaces
│   ├── traders.cpp          # Trader implementations and FFI bridge
│   ├── test_engine.cpp      # Unit tests for the engine
│   └── test_traders.cpp     # Unit tests for the traders
├── python_brain/            # Python ML Backend
│   ├── __init__.py          
│   └── traders.py           # Python bots and ML strategies
└── external/
    └── pybind11/            # Pybind11 Git submodule



🛠️ Prerequisites
Ensure you have the following installed on your system before building:

C++ Compiler: Supporting C++17 (e.g., GCC, Clang).

CMake: Version 3.14 or higher.

Python: Python 3.x.

Git: To clone the repository and submodules.




🏗️ Build Instructions

1. Clone the repository with submodules:

Bash
git clone --recursive <https://github.com/TobyMacDonald28/Market-Making-Simulation>
cd Market-Making-Simulation

2. Set up the Python Virtual Environment:

Bash
python3 -m venv venv
source venv/bin/activate  # On Windows: .\venv\Scripts\activate
pip install -r requirements.txt

3. Configure and Build the C++ Engine:

Bash
mkdir build && cd build
cmake ..
make



🏃‍♂️ Running the Simulation
Ensure your virtual environment is active, then run the compiled engine from the build directory:

Bash
source ../venv/bin/activate
./TradingEngine



🧪 Testing
The project includes a suite of unit tests to verify the integrity of the matching engine and trader logic. To run them:

Bash
cd build
./TraderTests
./OrderBookTests


⏱️ Benchmarking
A standalone benchmark measures throughput and latency percentiles (p50/p99/p99.9) for passive adds, cancels, aggressive fills, multi-level sweeps, market data reads, and a mixed market-maker workload on 1–8 threads. It is always compiled with -O3. Pass an optional op count (default 1,000,000):

Bash
cd build
make OrderBookBenchmark
./OrderBookBenchmark 1000000
