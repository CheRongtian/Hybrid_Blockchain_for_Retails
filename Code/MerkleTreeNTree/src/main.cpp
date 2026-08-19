#include "NTreeMerkle.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {

using merkle_ntree::BuildEvent;
using merkle_ntree::BuildResult;
using merkle_ntree::Node;
using merkle_ntree::NTreeMerkle;
using merkle_ntree::Proof;

struct Options
{
    std::optional<std::size_t> arity;
    std::optional<std::filesystem::path> input;
    std::optional<std::filesystem::path> output;
    std::optional<std::size_t> proof_index;
    std::vector<std::string> values;
    bool events = false;
    bool interactive = false;
};

std::string json_escape(const std::string& value)
{
    std::ostringstream result;
    for(const unsigned char character : value)
    {
        switch(character)
        {
            case '"': result << "\\\""; break;
            case '\\': result << "\\\\"; break;
            case '\b': result << "\\b"; break;
            case '\f': result << "\\f"; break;
            case '\n': result << "\\n"; break;
            case '\r': result << "\\r"; break;
            case '\t': result << "\\t"; break;
            default:
                if(character < 0x20)
                {
                    result << "\\u00";
                    const char* digits = "0123456789abcdef";
                    result << digits[(character >> 4) & 0x0f]
                           << digits[character & 0x0f];
                }
                else
                    result << static_cast<char>(character);
        }
    }
    return result.str();
}

std::string node_json(const Node& node)
{
    std::ostringstream result;
    result << "{\"id\":" << node.id
           << ",\"level\":" << node.level
           << ",\"index\":" << node.index
           << ",\"leaf\":" << (node.leaf ? "true" : "false")
           << ",\"padding\":" << (node.padding ? "true" : "false")
           << ",\"value\":\"" << json_escape(node.value) << "\""
           << ",\"hash\":\"" << json_escape(node.hash) << "\""
           << ",\"children\":[";
    for(std::size_t index = 0; index < node.children.size(); ++index)
    {
        if(index > 0) result << ',';
        result << node.children[index];
    }
    result << "]";
    if(node.duplicate_of)
        result << ",\"duplicateOf\":" << *node.duplicate_of;
    else
        result << ",\"duplicateOf\":null";
    result << '}';
    return result.str();
}

std::string nodes_json(const std::vector<Node>& nodes)
{
    std::ostringstream result;
    result << '[';
    for(std::size_t index = 0; index < nodes.size(); ++index)
    {
        if(index > 0) result << ',';
        result << node_json(nodes[index]);
    }
    result << ']';
    return result.str();
}

std::string event_json(const BuildEvent& event)
{
    std::ostringstream result;
    result << "{\"type\":\"" << json_escape(event.type)
           << "\",\"message\":\"" << json_escape(event.message)
           << "\",\"arity\":" << event.arity
           << ",\"level\":" << event.level;
    if(event.node_id)
        result << ",\"nodeId\":" << *event.node_id;
    else
        result << ",\"nodeId\":null";
    result << ",\"nodeHash\":\"" << json_escape(event.node_hash)
           << "\",\"rootHash\":\"" << json_escape(event.root_hash)
           << "\",\"nodes\":" << nodes_json(event.nodes) << '}';
    return result.str();
}

std::string proof_json(const Proof& proof, const bool verified,
                       const std::string& verification_error)
{
    std::ostringstream result;
    result << "{\"leafIndex\":" << proof.leaf_index
           << ",\"leafValue\":\"" << json_escape(proof.leaf_value)
           << "\",\"leafHash\":\"" << json_escape(proof.leaf_hash)
           << "\",\"rootHash\":\"" << json_escape(proof.root_hash)
           << "\",\"steps\":[";
    for(std::size_t index = 0; index < proof.steps.size(); ++index)
    {
        if(index > 0) result << ',';
        const auto& step = proof.steps[index];
        result << "{\"level\":" << step.level
               << ",\"childIndex\":" << step.child_index
               << ",\"siblingHashes\":[";
        for(std::size_t sibling = 0; sibling < step.sibling_hashes.size(); ++sibling)
        {
            if(sibling > 0) result << ',';
            result << "\"" << json_escape(step.sibling_hashes[sibling]) << "\"";
        }
        result << "]}";
    }
    result << "],\"verified\":" << (verified ? "true" : "false")
           << ",\"verificationError\":\""
           << json_escape(verification_error) << "\"}";
    return result.str();
}

std::string result_json(const BuildResult& result,
                        const std::optional<Proof>& proof,
                        const bool proof_verified,
                        const std::string& proof_error)
{
    std::ostringstream json;
    json << "{\"success\":" << (result.success ? "true" : "false")
         << ",\"error\":\"" << json_escape(result.error)
         << "\",\"rootHash\":\"" << json_escape(result.root_hash)
         << "\",\"arity\":" << result.arity
         << ",\"leafCount\":" << result.leaf_count
         << ",\"nodeCount\":" << result.node_count
         << ",\"height\":" << result.height
         << ",\"proofVerified\":" << (proof_verified ? "true" : "false");
    if(proof)
        json << ",\"proof\":" << proof_json(*proof, proof_verified, proof_error);
    else
        json << ",\"proof\":null";
    json << '}';
    return json.str();
}

std::size_t parse_size(const std::string& value, const std::string& name)
{
    std::size_t parsed = 0;
    std::size_t consumed = 0;
    try
    {
        parsed = std::stoull(value, &consumed);
    }
    catch(const std::exception&)
    {
        throw std::invalid_argument(name + " must be a non-negative integer.");
    }
    if(consumed != value.size())
        throw std::invalid_argument(name + " must be a non-negative integer.");
    return parsed;
}

void print_usage(const char* executable)
{
    std::cout
        << "Usage: " << executable << " [options]\n\n"
        << "Options:\n"
        << "  --arity N             Branching factor, N >= 2\n"
        << "  --input FILE          One non-empty leaf value per line\n"
        << "  --value TEXT          Add one leaf value; may be repeated\n"
        << "  --proof-index INDEX   Leaf index for the proof (default: 0)\n"
        << "  --output FILE         Write the final result JSON\n"
        << "  --events              Emit newline-delimited JSON build events\n"
        << "  --interactive         Read leaf values until an empty line\n"
        << "  --help                Show this help\n";
}

Options parse_options(const int argc, char** argv)
{
    Options options;
    for(int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        auto next_value = [&](const std::string& name) {
            if(index + 1 >= argc)
                throw std::invalid_argument(name + " requires a value.");
            return std::string(argv[++index]);
        };

        if(argument == "--arity")
            options.arity = parse_size(next_value(argument), "arity");
        else if(argument == "--input")
            options.input = std::filesystem::path(next_value(argument));
        else if(argument == "--value")
            options.values.push_back(next_value(argument));
        else if(argument == "--proof-index")
            options.proof_index = parse_size(next_value(argument), "proof-index");
        else if(argument == "--output")
            options.output = std::filesystem::path(next_value(argument));
        else if(argument == "--events")
            options.events = true;
        else if(argument == "--interactive")
            options.interactive = true;
        else if(argument == "--help" || argument == "-h")
        {
            print_usage(argv[0]);
            std::exit(0);
        }
        else
            throw std::invalid_argument("Unknown option: " + argument);
    }
    if(options.input && options.interactive)
        throw std::invalid_argument("--input and --interactive cannot be combined.");
    return options;
}

std::vector<std::string> read_input_file(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if(!input)
        throw std::runtime_error("Unable to open input file: " + path.string());

    std::vector<std::string> values;
    std::string line;
    while(std::getline(input, line))
    {
        if(!line.empty() && line.back() == '\r') line.pop_back();
        if(!line.empty()) values.push_back(line);
    }
    return values;
}

std::vector<std::string> read_interactive_values()
{
    std::vector<std::string> values;
    std::cerr << "Enter one leaf value per line. Submit an empty line to finish.\n";
    std::string line;
    while(std::getline(std::cin, line))
    {
        if(line.empty()) break;
        values.push_back(line);
    }
    return values;
}

void write_file(const std::filesystem::path& path, const std::string& content)
{
    if(path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if(!output)
        throw std::runtime_error("Unable to write output file: " + path.string());
    output << content << '\n';
}

void emit_extra_event(const std::string& type,
                      const std::string& message,
                      const NTreeMerkle& tree,
                      const bool enabled,
                      const std::optional<std::size_t>& node_id = std::nullopt,
                      const std::string& node_hash = "")
{
    if(!enabled) return;
    BuildEvent event;
    event.type = type;
    event.message = message;
    event.arity = tree.arity();
    event.level = tree.height() == 0 ? 0 : tree.height() - 1;
    event.node_id = node_id;
    event.node_hash = node_hash;
    event.root_hash = tree.root_hash();
    event.nodes = tree.nodes();
    std::cout << event_json(event) << '\n' << std::flush;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const Options options = parse_options(argc, argv);
        std::size_t arity = options.arity.value_or(0);
        if(arity == 0)
        {
            std::cerr << "Enter branching factor N (N >= 2): ";
            std::string value;
            if(!std::getline(std::cin, value))
                throw std::runtime_error("No branching factor was provided.");
            arity = parse_size(value, "arity");
        }

        std::vector<std::string> leaves = options.values;
        if(options.input)
        {
            const auto file_values = read_input_file(*options.input);
            leaves.insert(leaves.end(), file_values.begin(), file_values.end());
        }
        else if(options.interactive || leaves.empty())
        {
            const auto interactive_values = read_interactive_values();
            leaves.insert(leaves.end(), interactive_values.begin(), interactive_values.end());
        }

        NTreeMerkle tree(arity);
        const auto callback = [&](const BuildEvent& event) {
            if(options.events)
                std::cout << event_json(event) << '\n' << std::flush;
        };
        const BuildResult result = tree.build(leaves, callback);

        std::optional<Proof> proof;
        bool proof_verified = false;
        std::string proof_error;
        if(result.success)
        {
            const std::size_t proof_index = options.proof_index.value_or(0);
            try
            {
                proof = tree.generate_proof(proof_index);
                emit_extra_event("proof_generated", "Generated a Merkle Proof.",
                                 tree, options.events);
                proof_verified = tree.verify_proof(*proof, &proof_error);
                emit_extra_event(
                    proof_verified ? "proof_verified" : "proof_failed",
                    proof_verified
                        ? "Merkle Proof verification succeeded."
                        : proof_error,
                    tree, options.events);
            }
            catch(const std::exception& exception)
            {
                proof_error = exception.what();
                emit_extra_event("proof_failed", proof_error, tree, options.events);
            }
        }

        const std::string final_json = result_json(
            result, proof, proof_verified, proof_error);
        if(options.output) write_file(*options.output, final_json);

        if(!options.events)
        {
            if(result.success)
            {
                std::cout << "Build succeeded\n"
                          << "Arity: " << result.arity << '\n'
                          << "Leaves: " << result.leaf_count << '\n'
                          << "Nodes: " << result.node_count << '\n'
                          << "Height: " << result.height << '\n'
                          << "Root Hash: " << result.root_hash << '\n'
                          << "Proof: " << (proof_verified ? "verified" : "failed")
                          << '\n';
                if(!proof_verified && !proof_error.empty())
                    std::cout << "Proof error: " << proof_error << '\n';
            }
            else
                std::cerr << "Build failed: " << result.error << '\n';
        }

        return result.success && proof_verified ? 0 : 1;
    }
    catch(const std::exception& exception)
    {
        std::cerr << "Error: " << exception.what() << '\n';
        return 2;
    }
}
