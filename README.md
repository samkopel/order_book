# Order Books

A C++ project for managing order books using CMake.

## Project Structure

```
.
├── CMakeLists.txt       # CMake configuration
├── src/                 # Source files
│   └── main.cpp
├── include/             # Header files
├── build/               # Build output directory
└── README.md
```

## Requirements

- C++17 or later
- CMake 3.10 or later
- A C++ compiler (MSVC, GCC, or Clang)

## Building the Project

1. Navigate to the project root directory
2. Create a build directory (if not already created):
   ```bash
   mkdir build
   cd build
   ```
3. Configure the project:
   ```bash
   cmake ..
   ```
4. Build the project:
   ```bash
   cmake --build .
   ```

## Running the Project

After building, run the executable:

**On Windows:**
```bash
.\Debug\order_books.exe
```

**On Linux/Mac:**
```bash
./order_books
```

## Project Layout

- **src/** - Contains all source files (.cpp)
- **include/** - Contains all header files (.h, .hpp)
- **build/** - Build artifacts and compiled output
- **CMakeLists.txt** - CMake configuration file defining how to build the project