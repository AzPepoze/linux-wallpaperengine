#ifndef USER_PROPERTIES_H
#define USER_PROPERTIES_H

#include <string>
#include <utility>
#include <vector>

struct UserPropertyValue {
    enum class Type { Bool, Number, Color, Text };
    Type type = Type::Text;
    bool b = false;
    double n = 0;
    float color[3]{};
    std::string text;

    // The value in the form --set-property takes.
    std::string asText() const;
};

// A user property: its declared type from project.json plus its effective value.
struct UserPropertyDef {
    std::string key;
    std::string type;
    UserPropertyValue value;
};

// Effective values: project defaults, then saved GUI values, then --set-property.
class UserProperties {
   public:
    bool loadProject(const std::string& project_json_path);
    void applySaved(const std::string& gui_config_json_text, const std::string& workshop_id);
    void setFromString(const std::string& key, const std::string& raw);
    const UserPropertyValue* find(const std::string& key) const;
    const std::vector<UserPropertyDef>& all() const {
        return properties_;
    }
    // Every current value as key/text pairs, in the form --set-property takes.
    std::vector<std::pair<std::string, std::string>> toStrings() const;

   private:
    std::vector<UserPropertyDef> properties_;
};

// True when a changed key is read by a scene binding, so the scene must be rebuilt to show it.
bool touchesBoundKey(const std::vector<std::string>& bound_keys,
                     const std::vector<std::pair<std::string, std::string>>& changes);

#endif  // USER_PROPERTIES_H
