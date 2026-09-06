#pragma once
#include <cstdarg>
#include <string>
#include <iosfwd>
#include <set>
#include <list>
#include <map>
#include <mutex>

namespace ai
{
    class Qualified
    {
    public:
        Qualified() {};
        Qualified(const std::string& qualifier) : qualifier(qualifier) {}
        Qualified(int32 qualifier1) { Qualify(qualifier1); }

    public:
        virtual void Qualify(int32 qualifier) { std::ostringstream out; out << qualifier; this->qualifier = out.str(); }
        virtual void Qualify(const std::string& qualifier) { this->qualifier = qualifier; }
        std::string getQualifier() const { return qualifier; }
        void Reset() { qualifier.clear(); }

        static std::string MultiQualify(const std::vector<std::string>& qualifiers, const std::string& separator, const std::string_view brackets = "{}")
        { 
            std::stringstream out;
            for (uint8 i = 0; i < qualifiers.size(); i++)
            {
                const std::string& qualifier = qualifiers[i];
                if (i == qualifiers.size() - 1)
                {
                    out << qualifier;
                }
                else
                {
                    out << qualifier << separator;
                }
            }

            if (brackets.empty())
            {
                return out.str();
            }
            else
            {
                return brackets[0] + out.str() + brackets[1];
            }
        }

        static std::vector<std::string> getMultiQualifiers(const std::string& qualifier1, const std::string& separator, const std::string_view brackets = "{}")
        { 
            std::vector<std::string> result;

            std::string view = qualifier1;

            if(view.find(brackets[0]) == 0)
                view = qualifier1.substr(1, qualifier1.size()-2);

            size_t last = 0; 
            size_t next = 0; 

            if (view.find(brackets[0]) == std::string::npos)
            {
                while ((next = view.find(separator, last)) != std::string::npos)
                {

                    result.push_back((std::string)view.substr(last, next - last));
                    last = next + separator.length();
                }

                result.push_back(view.substr(last));
            }
            else
            {
                int8 level = 0;
                std::string sub;
                while (next < view.size() || level < 0)
                {
                    if (view[next] == brackets[0])
                        level++;
                    else if (view[next] == brackets[1])
                        level--;
                    else if (!level && view.substr(next, separator.size()) == separator)
                    {
                        result.push_back(sub);
                        sub.clear();
                        next += separator.size();
                        continue;
                    }
                    
                    sub += view[next];

                    next++;
                }

                result.push_back(sub);
            }

            return result;
        }

        static bool isValidNumberString(const std::string& str)
        {
            bool valid = !str.empty();
            if (valid)
            {
                // Check for sign character at the beginning
                size_t start = 0;
                if (str[0] == '+' || str[0] == '-')
                {
                    start = 1;
                }

                // Loop through each character to check if it's a digit
                for (size_t i = start; i < str.size(); ++i) 
                {
                    if (!std::isdigit(str[i])) 
                    {
                        // Non-numeric character found
                        valid = false;
                        break;
                    }
                }
            }

            return valid;
        }
        
        static int32 getMultiQualifierInt(const std::string& qualifier1, uint32 pos, const std::string& separator)
        { 
            std::vector<std::string> qualifiers = getMultiQualifiers(qualifier1, separator);
            if (qualifiers.size() > pos && isValidNumberString(qualifiers[pos]))
            {
                return stoi(qualifiers[pos]);
            }

            return 0;
        }

        static std::string getMultiQualifierStr(const std::string& qualifier1, uint32 pos, const std::string& separator)
        { 
            std::vector<std::string> qualifiers = getMultiQualifiers(qualifier1, separator);
            return (qualifiers.size() > pos) ? qualifiers[pos] : "";
        }
    
    protected:
        std::string qualifier;
    };

    template <class T>
    class NamedObjectFactory
    {
    protected:
        using ActionCreator = std::function<T* (PlayerbotAI* ai)>;
        std::map<std::string, ActionCreator> creators;

    public:
        T* Create(std::string_view name, PlayerbotAI* ai)
        {
            std::string_view nameView = name;
            std::string_view qualifierView;

            if (size_t pos = nameView.find("::"); pos != std::string::npos)
            {
                qualifierView = nameView.substr(pos + 2);
                nameView = nameView.substr(0, pos);
            }

            auto it = creators.find(std::string(nameView));
            if (it == creators.end())
                return nullptr;

            T* object = it->second(ai);
            if (object == nullptr)
                return nullptr;

            if (!qualifierView.empty())
            {
                if (auto* q = dynamic_cast<Qualified*>(object))
                    q->Qualify(std::string(qualifierView));
            }

            return object;
        }

        void GetSupportedKeys(std::set<std::string>& keys) const
        {
            for (const auto& entry : creators)
                keys.insert(entry.first);
        }
    };


    template <class T>
    class NamedObjectContext : public NamedObjectFactory<T>
    {
    public:
        NamedObjectContext(bool shared = false, bool supportsSiblings = false) :
            NamedObjectFactory<T>(), shared(shared), supportsSiblings(supportsSiblings) {}

        T* Create(std::string name, PlayerbotAI* ai)
        {
            std::lock_guard<std::recursive_mutex> lock(createdMutex);
            auto const existing = created.find(name);
            if (existing == created.end())
            {
                // Qualified lookups routinely probe unsupported combinations.
                // Do not retain a string/map node forever when no object was
                // created; at 4k bots those null entries consume substantial
                // memory and can grow for the lifetime of the process.
                T* object = NamedObjectFactory<T>::Create(name, ai);
                if (object)
                {
                    estimatedCreatedBytes += sizeof(typename decltype(created)::value_type) +
                        3 * sizeof(void*) + name.capacity() + 1 + sizeof(T);
                    created.emplace(std::move(name), object);
                }
                return object;
            }

            return existing->second;
        }

        virtual ~NamedObjectContext()
        {
            Clear();
        }

        void Clear()
        {
            std::lock_guard<std::recursive_mutex> lock(createdMutex);
            for (typename std::map<std::string, T*>::iterator i = created.begin(); i != created.end(); i++)
            {
                if (i->second)
                    delete i->second;
            }

            created.clear();
            estimatedCreatedBytes = 0;
        }

        void Erase(const std::string& name)
        {
            std::lock_guard<std::recursive_mutex> lock(createdMutex);
            typename std::map<std::string, T*>::iterator existing = created.find(name);
            if (existing != created.end())
            {
                auto const& entry = *existing;
                estimatedCreatedBytes -= sizeof(typename decltype(created)::value_type) +
                    3 * sizeof(void*) + entry.first.capacity() + 1 + (entry.second ? sizeof(T) : 0);
                delete entry.second;
                created.erase(existing);
            }
        }

        template <typename Predicate>
        size_t EraseIf(Predicate predicate)
        {
            std::lock_guard<std::recursive_mutex> lock(createdMutex);
            size_t erased = 0;
            for (auto existing = created.begin(); existing != created.end();)
            {
                if (!predicate(existing->first, existing->second))
                {
                    ++existing;
                    continue;
                }

                estimatedCreatedBytes -= sizeof(typename decltype(created)::value_type) +
                    3 * sizeof(void*) + existing->first.capacity() + 1 +
                    (existing->second ? sizeof(T) : 0);
                delete existing->second;
                existing = created.erase(existing);
                ++erased;
            }
            return erased;
        }

        void Update()
        {
            std::lock_guard<std::recursive_mutex> lock(createdMutex);
            for (typename std::map<std::string, T*>::iterator i = created.begin(); i != created.end(); i++)
            {
                if (i->second)
                    i->second->Update();
            }
        }

        void Reset()
        {
            std::lock_guard<std::recursive_mutex> lock(createdMutex);
            for (typename std::map<std::string, T*>::iterator i = created.begin(); i != created.end(); i++)
            {
                if (i->second)
                    i->second->Reset();
            }
        }

        bool IsShared() { return shared; }
        bool IsSupportsSiblings() { return supportsSiblings; }

        bool IsCreated(const std::string& name)
        {
            std::lock_guard<std::recursive_mutex> lock(createdMutex);
            return created.find(name) != created.end();
        }

        std::set<std::string> GetCreated()
        {
            std::lock_guard<std::recursive_mutex> lock(createdMutex);
            std::set<std::string> keys;
            for (typename std::map<std::string, T*>::iterator it = created.begin(); it != created.end(); it++)
                keys.insert(it->first);
            return keys;
        }

        size_t GetCreatedCount() const
        {
            std::lock_guard<std::recursive_mutex> lock(createdMutex);
            return created.size();
        }

        size_t GetEstimatedCreatedBytes() const
        {
            std::lock_guard<std::recursive_mutex> lock(createdMutex);
            return estimatedCreatedBytes;
        }

    protected:
        std::map<std::string, T*> created;
        mutable std::recursive_mutex createdMutex;
        size_t estimatedCreatedBytes = 0;
        bool shared;
        bool supportsSiblings;
    };

    template <class T> class NamedObjectContextList
    {
    public:
        virtual ~NamedObjectContextList()
        {
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                NamedObjectContext<T>* context = *i;
                if (!context->IsShared())
                    delete context;
            }
        }

        void Add(NamedObjectContext<T>* context)
        {
            contexts.push_back(context);
        }

        // Same as Add, but the context is consulted FIRST. Module contexts
        // need this: overriding a stock object by registering the same name
        // (dungeon clear's "auto release"/"loot roll") only works when the
        // module context sits ahead of the stock ones in GetObject's walk -
        // the first Create that answers wins.
        void AddFront(NamedObjectContext<T>* context)
        {
            contexts.push_front(context);
        }

        T* GetObject(const std::string& name, PlayerbotAI* ai)
        {
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                T* object = (*i)->Create(name, ai);
                if (object) return object;
            }
            return NULL;
        }

        void Update()
        {
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                if (!(*i)->IsShared())
                    (*i)->Update();
            }
        }

        void Reset()
        {
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                (*i)->Reset();
            }
        }

        std::set<std::string> GetSiblings(const std::string& name)
        {
            std::set<std::string> siblings;
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                if ((*i)->IsSupportsSiblings())
                {
                    std::set<std::string> supported;
                    (*i)->GetSupportedKeys(supported);
                    std::set<std::string>::iterator found = supported.find(name);
                    if (found != supported.end())
                    {
                        supported.erase(found);
                        siblings.insert(supported.begin(), supported.end());
                    }
                }
            }

            return siblings;
        }

        void GetSupportedKeys(std::set<std::string>& keys) const
        {
            for (typename std::list<NamedObjectContext<T>*>::const_iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                (*i)->GetSupportedKeys(keys);
            }
        }

        bool IsCreated(const std::string& name) const
        {
            for (typename std::list<NamedObjectContext<T>*>::const_iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                if ((*i)->IsCreated(name))
                    return true;
            }
            return false;
        }

        std::set<std::string> GetCreated()
        {
            std::set<std::string> result;

            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                std::set<std::string> createdKeys = (*i)->GetCreated();

                for (std::set<std::string>::iterator j = createdKeys.begin(); j != createdKeys.end(); j++)
                    result.insert(*j);
            }
            return result;
        }

        size_t GetCreatedCount() const
        {
            size_t count = 0;
            for (typename std::list<NamedObjectContext<T>*>::const_iterator i = contexts.begin(); i != contexts.end(); ++i)
                count += (*i)->GetCreatedCount();
            return count;
        }

        size_t GetEstimatedCreatedBytes() const
        {
            size_t bytes = 0;
            for (auto const* context : contexts) bytes += context->GetEstimatedCreatedBytes();
            return bytes;
        }

        void Erase(const std::string& name)
        {
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                (*i)->Erase(name);
            }
        }

        template <typename Predicate>
        size_t EraseIf(Predicate predicate)
        {
            size_t erased = 0;
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); ++i)
                erased += (*i)->EraseIf(predicate);
            return erased;
        }

    private:
        std::list<NamedObjectContext<T>*> contexts;
    };

    template <class T>
    class NamedObjectFactoryList
    {
    public:
        void Add(std::unique_ptr<NamedObjectFactory<T>> factory)
        {
            factories.emplace_back(std::move(factory));
        }

        T* GetObject(std::string_view name, PlayerbotAI* ai)
        {
            for (auto it = factories.rbegin(); it != factories.rend(); ++it)
            {
                if (T* obj = (*it)->Create(name, ai))
                    return obj;
            }
            return nullptr;
        }

    private:
        std::vector<std::unique_ptr<NamedObjectFactory<T>>> factories;
    };
};
