# SingIt

**SingIt** is a local desktop application for searching your music library by the **meaning of song lyrics**, rather than requiring an exact keyword or phrase match.

Instead of searching for:

> "You're taking me out of the ordinary"

and requiring those exact words to appear in a lyric, SingIt can use semantic similarity to find lyrics expressing a similar idea—even when completely different words are used.

Once a matching lyric is found, SingIt can open the corresponding MP3 file and play the relevant portion of the song using the timestamps associated with the lyric.

The entire semantic-search pipeline runs **locally on the user's computer**. No client/server application or cloud AI API is required for the core search functionality.

---

## Features

- 🔎 **Semantic lyric search**
  - Search lyrics by meaning rather than exact text.
  - Uses neural sentence embeddings to represent lyric passages numerically.
  - Finds semantically similar lyrics using vector similarity.

- 🎵 **Music playback**
  - Opens the corresponding local MP3 file.
  - Starts playback at the timestamp associated with the matching lyric.
  - Stops playback at the end of the lyric section.

- 🧠 **Local AI inference**
  - Uses the BAAI BGE-small English embedding model.
  - Model inference is performed locally using ONNX Runtime.
  - No external AI API is required for generating search embeddings.

- 🗄️ **Local database**
  - SQLite stores song metadata, lyrics, timestamps, and embeddings.
  - `sqlite-vec` provides vector similarity search.

- 🔐 **No required cloud backend**
  - The application is designed as a self-contained desktop application.
  - Music files and the semantic-search database remain local.

---

## How It Works

SingIt's search pipeline can be summarized as:

```text
User enters a search phrase
          │
          ▼
      Tokenizer
          │
          ▼
   BGE-small-en-v1.5
          │
          ▼
  384-dimensional embedding
          │
          ▼
       SQLite
     + sqlite-vec
          │
          ▼
Nearest lyric embeddings
          │
          ▼
 Song + lyric + timestamps
          │
          ▼
      QMediaPlayer
          │
          ▼
   Relevant MP3 section
```

### 1. Text is Tokenized

The user's search text is processed using the BGE model's tokenizer.

SingIt uses **tokenizers-cpp** to perform tokenization locally.

### 2. An Embedding Is Generated

The tokenized input is passed to the BGE-small English model through **ONNX Runtime**.

The model produces a 384-dimensional representation of the text.

SingIt performs mean pooling over the token representations and then L2-normalizes the resulting vector.

### 3. Vector Similarity Search

The resulting 384-dimensional vector is searched against the lyric embeddings stored in SQLite using **sqlite-vec**.

This allows the application to find lyrics that are semantically similar to the user's query.

### 4. The Matching Lyric Is Identified

Each embedding is associated with information such as:

- Artist
- Track name
- MP3 filename
- File number
- Lyric text
- Start timestamp
- Stop timestamp

### 5. The Relevant Part of the Song Is Played

After a result is selected, SingIt opens the corresponding local MP3 and uses the lyric timestamps to play the relevant section of the song.

---

# Technologies and Packages

## C++ / Qt

### Qt 6

SingIt is a C++ desktop application built using **Qt 6**.

Qt provides:

- GUI functionality
- Windows desktop integration
- `QMediaPlayer` audio playback
- File and path handling
- Signals and slots
- Application event handling

The application is currently developed using:

- **Qt 6.11.2**
- **Qt Widgets**
- **Qt Multimedia**
- **C++17**

---

## CMake

The project uses **CMake** as its build system.

CMake is responsible for:

- Configuring the project
- Building the C++ sources
- Linking Qt
- Building and linking third-party components
- Including SQLite
- Configuring the application executable
- Deploying required runtime dependencies

---

## ONNX Runtime

SingIt uses **ONNX Runtime** to execute the neural-network model locally.

ONNX Runtime provides the inference engine used to execute:

**BAAI/bge-small-en-v1.5**

The model produces 384-dimensional embeddings that are used for semantic lyric search.

The ONNX Runtime distribution is included under the project's third-party dependencies.

---

## BAAI BGE-small-en-v1.5

SingIt uses the **BGE-small-en-v1.5** sentence embedding model.

The model converts text into a numerical vector representing its semantic meaning.

For example, phrases such as:

```text
"There is some crazy person on my lawn"
```

and:

```text
"The lunatic is on the grass..."
```

can produce vectors that are close to one another even though the words are substantially different.

This is what allows SingIt to search lyrics based on meaning.

The model is stored locally and executed through ONNX Runtime.

---

## tokenizers-cpp

SingIt uses **tokenizers-cpp** for local text tokenization.

The tokenizer converts user input into the token IDs expected by the BGE ONNX model.

The project uses the Hugging Face tokenizer data associated with the model.

---

## SQLite

**SQLite** is used as SingIt's local database engine.

The database contains information such as:

- Songs
- Artists
- MP3 filenames
- File numbers
- Lyrics
- Lyric timestamps
- Vector embeddings

SQLite was selected because SingIt is intended to operate as a local desktop application without requiring a database server.

The SQLite amalgamation is included in the project's third-party directory.

---

## sqlite-vec

**sqlite-vec** provides vector search capabilities inside SQLite.

This allows SingIt to perform nearest-neighbor searches against the 384-dimensional lyric embeddings.

Conceptually, the search is:

```text
User query embedding
        │
        ▼
     sqlite-vec
        │
        ▼
Nearest lyric vectors
        │
        ▼
Most semantically similar lyrics
```

This makes it possible to combine conventional relational data and vector search in a single local database.

---

## Qt Multimedia

SingIt uses Qt Multimedia, specifically:

```cpp
QMediaPlayer
```

for MP3 playback.

The application uses the timestamps associated with synchronized lyrics to determine which portion of a song should be played.

---

# Data Sources

SingIt was developed around a local music library and synchronized lyric data.

The lyric data is based on the **LRCLIB database dump**, which provides synchronized lyrics and track information.

The application processes synchronized lyric timestamps such as:

```text
[01:23.45] lyric text
[01:27.82] next lyric
[01:31.10] another lyric
```

These timestamps are converted into start and stop times for individual lyric sections.

---

# Project Structure

The project contains several major components.

```text
SingIt/
│
├── main.cpp
├── mainwindow.cpp
├── mainwindow.h
├── mainwindow.ui
│
├── musicdatabase.cpp
├── musicdatabase.h
│
├── models/
│   ├── tokenizer.json
│   └── model.onnx
│
├── third_party/
│   ├── onnxruntime/
│   ├── tokenizers-cpp/
│   └── sqlite/
│
└── CMakeLists.txt
```

The exact contents may change as development continues.

---

# Database Design

The database associates lyric embeddings with the song and timestamp information needed to play the matching section.

A conceptual record looks like:

```text
Artist
Track Name
Filename
File Number
Start Time
Stop Time
Lyric Text
Embedding[384]
```

The embedding is the normalized 384-dimensional vector generated by the BGE model.

---

# Requirements

A development environment currently consists of:

- Windows 11
- Qt 6.11.x
- Qt Creator
- CMake
- MinGW 64-bit
- C++17-compatible compiler
- ONNX Runtime
- tokenizers-cpp
- SQLite
- sqlite-vec

The application itself is intended to be distributed as a Windows executable with the required Qt and runtime dependencies.

An Inno Setup installer is used to package the application for distribution.

---

# Building

The project is built using CMake and Qt Creator.

A typical build configuration uses:

```text
Qt 6.11.2
MinGW 64-bit
C++17
```

The project also requires the model and tokenizer files to be available at runtime.

After building, the application must have access to the required:

- Qt DLLs
- ONNX Runtime DLLs
- Model files
- Tokenizer files
- Other runtime dependencies

The deployment configuration copies these files into the application's output directory.

---

# Installation

The Windows distribution is packaged using **Inno Setup**.

The installer creates a standard Windows installation package:

```text
SingItSetup.exe
```

The installer contains the application executable and the runtime files required by SingIt.

---

# Music and Copyright Disclaimer

**Users are responsible for ensuring that they have the legal right to use any music files loaded into SingIt.**

SingIt is designed to work with music files stored locally on the user's computer. The application does not grant any rights to copy, download, distribute, or otherwise use copyrighted music.

You should only use music that you legally own or otherwise have permission to use.

The developers of SingIt are not responsible for a user's unauthorized use, copying, distribution, or other infringement of copyrighted music.

---

# Privacy and Local Processing

One of the design goals of SingIt is to keep the semantic-search process local.

The application does not need to send a user's search query to a remote AI service.

The embedding model runs locally through ONNX Runtime, and vector searches are performed locally through SQLite and sqlite-vec.

The user's music library also remains on the local machine.

---

# Development

SingIt is both a practical application and an exploration of several technologies working together:

- C++
- Qt
- CMake
- Neural-network inference
- Sentence embeddings
- Natural-language processing
- Vector databases
- SQLite
- Audio playback
- Windows application deployment

A significant amount of the development process involved experimenting with and integrating technologies that are normally encountered separately.

---

# Use of ChatGPT

**ChatGPT was heavily used throughout the development of SingIt.**

It was used as a development assistant for a wide range of tasks, including:

- C++ programming assistance
- Qt development
- CMake configuration
- Debugging compiler and linker errors
- Understanding ONNX Runtime
- Understanding transformer model inputs and outputs
- Working with tokenizers-cpp
- Designing the SQLite database interface
- Integrating sqlite-vec
- Debugging vector-search queries
- MP3 playback with Qt Multimedia
- Windows deployment
- Dependency management
- Inno Setup configuration
- Explaining C++ language concepts and APIs

The project was developed interactively, with ChatGPT frequently being used to reason through errors, explain unfamiliar technologies, propose implementations, and refine existing code.

ChatGPT was an **engineering aid**, not an autonomous developer. The resulting code was tested, modified, debugged, and integrated into the project during development.

---

# Project Status

SingIt is an active development project.

The core semantic-search pipeline is operational:

```text
Text
 → Tokenization
 → BGE embedding
 → 384-dimensional vector
 → sqlite-vec search
 → Matching lyric
 → Song metadata
 → MP3 playback
```

The project is continuing to evolve, particularly around application packaging, deployment, user interface improvements, and overall usability.

---

# License

Copyright (c) 2026 Michael

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

# Acknowledgements

SingIt builds on several excellent open-source projects and technologies:

- Qt
- SQLite
- sqlite-vec
- ONNX Runtime
- Hugging Face Transformers/tokenization ecosystem
- tokenizers-cpp
- BAAI BGE models
- LRCLIB

Special thanks to the open-source communities behind these projects.

And a special acknowledgement to **ChatGPT**, which was heavily used throughout the development process as a programming, debugging, research, and technical-learning assistant.
