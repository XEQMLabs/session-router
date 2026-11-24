#include "auth.hpp"
#include "crypto/crypto.hpp"
#include "router/router.hpp"
#include "util/str.hpp"

#include <sodium/utils.h>

namespace srouter::auth
{
    AuthResult FileAuthPolicy::check_files(const AuthInfo& info) const
    {
        for (const auto& f : _files)
        {
            std::ifstream i{f};
            std::string line{};
            while (std::getline(i, line))
            {
                // split off comments
                const auto parts = split_any(line, "#;", true);
                // Check emptiness before indexing parts[0]: split_any can return an empty vector, so
                // the previous `auto part = parts[0]` read out of bounds on a blank line.
                if (not parts.empty() and not parts[0].empty())
                {
                    // split off whitespaces and check password
                    if (check_passwd(std::string{TrimWhitespace(parts[0])}, info.token))
                        return AuthResult{AuthCode::ACCEPTED, "accepted by whitelist"};
                }
            }
        }
        return AuthResult{AuthCode::REJECTED, "rejected by whitelist"};
    }

    bool FileAuthPolicy::check_passwd(std::string hash, std::string challenge) const
    {
        switch (_type)
        {
            case AuthFileType::PLAIN:
                // Constant-time compare so a plaintext token cannot be recovered byte-by-byte from
                // response timing.
                return hash.size() == challenge.size()
                    && sodium_memcmp(hash.data(), challenge.data(), hash.size()) == 0;
            case AuthFileType::HASHES:
                return crypto::check_passwd_hash(std::move(hash), std::move(challenge));
        }
        return false;
    }

}  // namespace srouter::auth
