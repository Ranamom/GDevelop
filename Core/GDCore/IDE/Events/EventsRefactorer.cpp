/*
 * GDevelop Core
 * Copyright 2008-2016 Florian Rival (Florian.Rival@gmail.com). All rights
 * reserved. This project is released under the MIT License.
 */

#include "GDCore/IDE/Events/EventsRefactorer.h"

#include <memory>

#include "GDCore/CommonTools.h"
#include "GDCore/Events/Event.h"
#include "GDCore/Events/EventsList.h"
#include "GDCore/Events/Parsers/ExpressionParser2NodePrinter.h"
#include "GDCore/Events/Parsers/ExpressionParser2NodeWorker.h"
#include "GDCore/Extensions/Metadata/ExpressionMetadata.h"
#include "GDCore/Extensions/Metadata/MetadataProvider.h"
#include "GDCore/Extensions/Platform.h"
#include "GDCore/IDE/Events/ExpressionValidator.h"
#include "GDCore/IDE/Events/InstructionSentenceFormatter.h"
#include "GDCore/Project/ObjectsContainer.h"
#include "GDCore/Project/EventsBasedObject.h"
#include "GDCore/IDE/Events/ExpressionTypeFinder.h"

using namespace std;

namespace gd {

const gd::String EventsRefactorer::searchIgnoredCharacters = ";:,#()";

/**
 * \brief Go through the nodes and change the given object name to a new one.
 *
 * \see gd::ExpressionParser2
 */
class GD_CORE_API ExpressionObjectRenamer : public ExpressionParser2NodeWorker {
 public:
  ExpressionObjectRenamer(const gd::Platform &platform_,
                          const gd::ObjectsContainer &globalObjectsContainer_,
                          const gd::ObjectsContainer &objectsContainer_,
                          const gd::String &rootType_,
                          const gd::String& objectName_,
                          const gd::String& objectNewName_)
      : platform(platform_),
        globalObjectsContainer(globalObjectsContainer_),
        objectsContainer(objectsContainer_),
        rootType(rootType_),
        hasDoneRenaming(false),
        objectName(objectName_),
        objectNewName(objectNewName_){};
  virtual ~ExpressionObjectRenamer(){};

  static bool Rename(const gd::Platform &platform,
                     const gd::ObjectsContainer &globalObjectsContainer,
                     const gd::ObjectsContainer &objectsContainer,
                     const gd::String &rootType,
                     gd::ExpressionNode& node,
                     const gd::String& objectName,
                     const gd::String& objectNewName) {
    if (gd::ExpressionValidator::HasNoErrors(platform, globalObjectsContainer, objectsContainer, rootType, node)) {
      ExpressionObjectRenamer renamer(platform, globalObjectsContainer, objectsContainer, rootType, objectName, objectNewName);
      node.Visit(renamer);

      return renamer.HasDoneRenaming();
    }

    return false;
  }

  bool HasDoneRenaming() const { return hasDoneRenaming; }

 protected:
  void OnVisitSubExpressionNode(SubExpressionNode& node) override {
    node.expression->Visit(*this);
  }
  void OnVisitOperatorNode(OperatorNode& node) override {
    node.leftHandSide->Visit(*this);
    node.rightHandSide->Visit(*this);
  }
  void OnVisitUnaryOperatorNode(UnaryOperatorNode& node) override {
    node.factor->Visit(*this);
  }
  void OnVisitNumberNode(NumberNode& node) override {}
  void OnVisitTextNode(TextNode& node) override {}
  void OnVisitVariableNode(VariableNode& node) override {
    if (node.child) node.child->Visit(*this);
  }
  void OnVisitVariableAccessorNode(VariableAccessorNode& node) override {
    if (node.child) node.child->Visit(*this);
  }
  void OnVisitVariableBracketAccessorNode(
      VariableBracketAccessorNode& node) override {
    node.expression->Visit(*this);
    if (node.child) node.child->Visit(*this);
  }
  void OnVisitIdentifierNode(IdentifierNode& node) override {
    auto type = gd::ExpressionTypeFinder::GetType(platform, globalObjectsContainer, objectsContainer, rootType, node);
    if (gd::ParameterMetadata::IsObject(type) &&
        node.identifierName == objectName) {
      hasDoneRenaming = true;
      node.identifierName = objectNewName;
    }
  }
  void OnVisitObjectFunctionNameNode(ObjectFunctionNameNode& node) override {
    if (node.objectName == objectName) {
      hasDoneRenaming = true;
      node.objectName = objectNewName;
    }
  }
  void OnVisitFunctionCallNode(FunctionCallNode& node) override {
    if (node.objectName == objectName) {
      hasDoneRenaming = true;
      node.objectName = objectNewName;
    }
    for (auto& parameter : node.parameters) {
      parameter->Visit(*this);
    }
  }
  void OnVisitEmptyNode(EmptyNode& node) override {}

 private:
  bool hasDoneRenaming;
  const gd::String& objectName;
  const gd::String& objectNewName;

  const gd::Platform &platform;
  const gd::ObjectsContainer &globalObjectsContainer;
  const gd::ObjectsContainer &objectsContainer;
  const gd::String rootType;
};

/**
 * \brief Go through the nodes and check if the given object is being used
 * in the expression.
 *
 * \see gd::ExpressionParser2
 */
class GD_CORE_API ExpressionObjectFinder : public ExpressionParser2NodeWorker {
 public:
  ExpressionObjectFinder(const gd::Platform &platform_,
                         const gd::ObjectsContainer &globalObjectsContainer_,
                         const gd::ObjectsContainer &objectsContainer_,
                         const gd::String &rootType_,
                         const gd::String& objectName_)
      : platform(platform_),
        globalObjectsContainer(globalObjectsContainer_),
        objectsContainer(objectsContainer_),
        rootType(rootType_),
        hasObject(false),
        objectName(objectName_){};
  virtual ~ExpressionObjectFinder(){};

  static bool CheckIfHasObject(const gd::Platform &platform,
                               const gd::ObjectsContainer &globalObjectsContainer,
                               const gd::ObjectsContainer &objectsContainer,
                               const gd::String &rootType,
                               gd::ExpressionNode& node,
                               const gd::String& objectName) {
    if (gd::ExpressionValidator::HasNoErrors(platform, globalObjectsContainer, objectsContainer, rootType, node)) {
      ExpressionObjectFinder finder(platform, globalObjectsContainer, objectsContainer, rootType, objectName);
      node.Visit(finder);

      return finder.HasFoundObject();
    }

    return false;
  }

  bool HasFoundObject() const { return hasObject; }

 protected:
  void OnVisitSubExpressionNode(SubExpressionNode& node) override {
    node.expression->Visit(*this);
  }
  void OnVisitOperatorNode(OperatorNode& node) override {
    node.leftHandSide->Visit(*this);
    node.rightHandSide->Visit(*this);
  }
  void OnVisitUnaryOperatorNode(UnaryOperatorNode& node) override {
    node.factor->Visit(*this);
  }
  void OnVisitNumberNode(NumberNode& node) override {}
  void OnVisitTextNode(TextNode& node) override {}
  void OnVisitVariableNode(VariableNode& node) override {
    if (node.child) node.child->Visit(*this);
  }
  void OnVisitVariableAccessorNode(VariableAccessorNode& node) override {
    if (node.child) node.child->Visit(*this);
  }
  void OnVisitVariableBracketAccessorNode(
      VariableBracketAccessorNode& node) override {
    node.expression->Visit(*this);
    if (node.child) node.child->Visit(*this);
  }
  void OnVisitIdentifierNode(IdentifierNode& node) override {
    auto type = gd::ExpressionTypeFinder::GetType(platform, globalObjectsContainer, objectsContainer, rootType, node);
    if (gd::ParameterMetadata::IsObject(type) &&
        node.identifierName == objectName) {
      hasObject = true;
    }
  }
  void OnVisitObjectFunctionNameNode(ObjectFunctionNameNode& node) override {
    if (node.objectName == objectName) {
      hasObject = true;
    }
  }
  void OnVisitFunctionCallNode(FunctionCallNode& node) override {
    if (node.objectName == objectName) {
      hasObject = true;
    }
    for (auto& parameter : node.parameters) {
      parameter->Visit(*this);
    }
  }
  void OnVisitEmptyNode(EmptyNode& node) override {}

 private:
  bool hasObject;
  const gd::String& objectName;

  const gd::Platform &platform;
  const gd::ObjectsContainer &globalObjectsContainer;
  const gd::ObjectsContainer &objectsContainer;
  const gd::String rootType;
};

bool EventsRefactorer::RenameObjectInActions(const gd::Platform& platform,
                                             gd::ObjectsContainer& project,
                                             gd::ObjectsContainer& layout,
                                             gd::InstructionsList& actions,
                                             gd::String oldName,
                                             gd::String newName) {
  bool somethingModified = false;

  for (std::size_t aId = 0; aId < actions.size(); ++aId) {
    const gd::InstructionMetadata& instrInfos =
        MetadataProvider::GetActionMetadata(platform, actions[aId].GetType());
    for (std::size_t pNb = 0; pNb < instrInfos.parameters.size(); ++pNb) {
      // Replace object's name in parameters
      if (gd::ParameterMetadata::IsObject(instrInfos.parameters[pNb].GetType()) &&
          actions[aId].GetParameter(pNb).GetPlainString() == oldName)
        actions[aId].SetParameter(pNb, gd::Expression(newName));
      // Replace object's name in expressions
      else if (ParameterMetadata::IsExpression(
                   "number", instrInfos.parameters[pNb].GetType())) {
        auto node = actions[aId].GetParameter(pNb).GetRootNode();

        if (ExpressionObjectRenamer::Rename(platform, project, layout, "number", *node, oldName, newName)) {
          actions[aId].SetParameter(
              pNb, ExpressionParser2NodePrinter::PrintNode(*node));
        }
      }
      // Replace object's name in text expressions
      else if (ParameterMetadata::IsExpression(
                   "string", instrInfos.parameters[pNb].GetType())) {
        auto node = actions[aId].GetParameter(pNb).GetRootNode();

        if (ExpressionObjectRenamer::Rename(platform, project, layout, "string", *node, oldName, newName)) {
          actions[aId].SetParameter(
              pNb, ExpressionParser2NodePrinter::PrintNode(*node));
        }
      }
    }

    if (!actions[aId].GetSubInstructions().empty())
      somethingModified =
          RenameObjectInActions(platform,
                                project,
                                layout,
                                actions[aId].GetSubInstructions(),
                                oldName,
                                newName) ||
          somethingModified;
  }

  return somethingModified;
}

bool EventsRefactorer::RenameObjectInConditions(
    const gd::Platform& platform,
    gd::ObjectsContainer& project,
    gd::ObjectsContainer& layout,
    gd::InstructionsList& conditions,
    gd::String oldName,
    gd::String newName) {
  bool somethingModified = false;

  for (std::size_t cId = 0; cId < conditions.size(); ++cId) {
    const gd::InstructionMetadata& instrInfos =
        MetadataProvider::GetConditionMetadata(platform,
                                               conditions[cId].GetType());
    for (std::size_t pNb = 0; pNb < instrInfos.parameters.size(); ++pNb) {
      // Replace object's name in parameters
      if (gd::ParameterMetadata::IsObject(instrInfos.parameters[pNb].GetType()) &&
          conditions[cId].GetParameter(pNb).GetPlainString() == oldName)
        conditions[cId].SetParameter(pNb, gd::Expression(newName));
      // Replace object's name in expressions
      else if (ParameterMetadata::IsExpression(
                   "number", instrInfos.parameters[pNb].GetType())) {
        auto node = conditions[cId].GetParameter(pNb).GetRootNode();

        if (ExpressionObjectRenamer::Rename(platform, project, layout, "number", *node, oldName, newName)) {
          conditions[cId].SetParameter(
              pNb, ExpressionParser2NodePrinter::PrintNode(*node));
        }
      }
      // Replace object's name in text expressions
      else if (ParameterMetadata::IsExpression(
                   "string", instrInfos.parameters[pNb].GetType())) {
        auto node = conditions[cId].GetParameter(pNb).GetRootNode();

        if (ExpressionObjectRenamer::Rename(platform, project, layout, "string", *node, oldName, newName)) {
          conditions[cId].SetParameter(
              pNb, ExpressionParser2NodePrinter::PrintNode(*node));
        }
      }
    }

    if (!conditions[cId].GetSubInstructions().empty())
      somethingModified =
          RenameObjectInConditions(platform,
                                   project,
                                   layout,
                                   conditions[cId].GetSubInstructions(),
                                   oldName,
                                   newName) ||
          somethingModified;
  }

  return somethingModified;
}

bool EventsRefactorer::RenameObjectInEventParameters(
    const gd::Platform& platform,
    gd::ObjectsContainer& project,
    gd::ObjectsContainer& layout,
    gd::Expression& expression,
    gd::ParameterMetadata parameterMetadata,
    gd::String oldName,
    gd::String newName) {
  bool somethingModified = false;

  if (gd::ParameterMetadata::IsObject(parameterMetadata.GetType()) &&
      expression.GetPlainString() == oldName)
    expression = gd::Expression(newName);
  // Replace object's name in expressions
  else if (ParameterMetadata::IsExpression("number",
                                           parameterMetadata.GetType())) {
    auto node = expression.GetRootNode();

    if (ExpressionObjectRenamer::Rename(platform, project, layout, "number", *node, oldName, newName)) {
      expression = ExpressionParser2NodePrinter::PrintNode(*node);
    }
  }
  // Replace object's name in text expressions
  else if (ParameterMetadata::IsExpression("string",
                                           parameterMetadata.GetType())) {
    auto node = expression.GetRootNode();

    if (ExpressionObjectRenamer::Rename(platform, project, layout, "string", *node, oldName, newName)) {
      expression = ExpressionParser2NodePrinter::PrintNode(*node);
    }
  }

  return somethingModified;
}

void EventsRefactorer::RenameObjectInEvents(const gd::Platform& platform,
                                            gd::ObjectsContainer& project,
                                            gd::ObjectsContainer& layout,
                                            gd::EventsList& events,
                                            gd::String oldName,
                                            gd::String newName) {
  for (std::size_t i = 0; i < events.size(); ++i) {
    vector<gd::InstructionsList*> conditionsVectors =
        events[i].GetAllConditionsVectors();
    for (std::size_t j = 0; j < conditionsVectors.size(); ++j) {
      bool somethingModified = RenameObjectInConditions(
          platform, project, layout, *conditionsVectors[j], oldName, newName);
    }

    vector<gd::InstructionsList*> actionsVectors =
        events[i].GetAllActionsVectors();
    for (std::size_t j = 0; j < actionsVectors.size(); ++j) {
      bool somethingModified = RenameObjectInActions(
          platform, project, layout, *actionsVectors[j], oldName, newName);
    }

    vector<pair<gd::Expression*, gd::ParameterMetadata>>
        expressionsWithMetadata = events[i].GetAllExpressionsWithMetadata();
    for (std::size_t j = 0; j < expressionsWithMetadata.size(); ++j) {
      gd::Expression* expression = expressionsWithMetadata[j].first;
      gd::ParameterMetadata parameterMetadata =
          expressionsWithMetadata[j].second;
      bool somethingModified = RenameObjectInEventParameters(platform,
                                                             project,
                                                             layout,
                                                             *expression,
                                                             parameterMetadata,
                                                             oldName,
                                                             newName);
    }

    if (events[i].CanHaveSubEvents())
      RenameObjectInEvents(platform,
                           project,
                           layout,
                           events[i].GetSubEvents(),
                           oldName,
                           newName);
  }
}

bool EventsRefactorer::RemoveObjectInActions(const gd::Platform& platform,
                                             gd::ObjectsContainer& globalObjectsContainer,
                                             gd::ObjectsContainer& objectsContainer,
                                             gd::InstructionsList& actions,
                                             gd::String name) {
  bool somethingModified = false;

  for (std::size_t aId = 0; aId < actions.size(); ++aId) {
    bool deleteMe = false;

    const gd::InstructionMetadata& instrInfos =
        MetadataProvider::GetActionMetadata(platform, actions[aId].GetType());
    for (std::size_t pNb = 0; pNb < instrInfos.parameters.size(); ++pNb) {
      // Find object's name in parameters
      if (gd::ParameterMetadata::IsObject(instrInfos.parameters[pNb].GetType()) &&
          actions[aId].GetParameter(pNb).GetPlainString() == name) {
        deleteMe = true;
        break;
      }
      // Find object's name in expressions
      else if (ParameterMetadata::IsExpression(
                   "number", instrInfos.parameters[pNb].GetType())) {
        auto node = actions[aId].GetParameter(pNb).GetRootNode();

        if (ExpressionObjectFinder::CheckIfHasObject(platform, globalObjectsContainer, objectsContainer, "number", *node, name)) {
          deleteMe = true;
          break;
        }
      }
      // Find object's name in text expressions
      else if (ParameterMetadata::IsExpression(
                   "string", instrInfos.parameters[pNb].GetType())) {
        auto node = actions[aId].GetParameter(pNb).GetRootNode();

        if (ExpressionObjectFinder::CheckIfHasObject(platform, globalObjectsContainer, objectsContainer, "string", *node, name)) {
          deleteMe = true;
          break;
        }
      }
    }

    if (deleteMe) {
      somethingModified = true;
      actions.Remove(aId);
      aId--;
    } else if (!actions[aId].GetSubInstructions().empty())
      somethingModified =
          RemoveObjectInActions(platform,
                                globalObjectsContainer,
                                objectsContainer,
                                actions[aId].GetSubInstructions(),
                                name) ||
          somethingModified;
  }

  return somethingModified;
}

bool EventsRefactorer::RemoveObjectInConditions(
    const gd::Platform& platform,
    gd::ObjectsContainer& globalObjectsContainer,
    gd::ObjectsContainer& objectsContainer,
    gd::InstructionsList& conditions,
    gd::String name) {
  bool somethingModified = false;

  for (std::size_t cId = 0; cId < conditions.size(); ++cId) {
    bool deleteMe = false;

    const gd::InstructionMetadata& instrInfos =
        MetadataProvider::GetConditionMetadata(platform,
                                               conditions[cId].GetType());
    for (std::size_t pNb = 0; pNb < instrInfos.parameters.size(); ++pNb) {
      // Find object's name in parameters
      if (gd::ParameterMetadata::IsObject(instrInfos.parameters[pNb].GetType()) &&
          conditions[cId].GetParameter(pNb).GetPlainString() == name) {
        deleteMe = true;
        break;
      }
      // Find object's name in expressions
      else if (ParameterMetadata::IsExpression(
                   "number", instrInfos.parameters[pNb].GetType())) {
        auto node = conditions[cId].GetParameter(pNb).GetRootNode();

        if (ExpressionObjectFinder::CheckIfHasObject(platform, globalObjectsContainer, objectsContainer, "number", *node, name)) {
          deleteMe = true;
          break;
        }
      }
      // Find object's name in text expressions
      else if (ParameterMetadata::IsExpression(
                   "string", instrInfos.parameters[pNb].GetType())) {
        auto node = conditions[cId].GetParameter(pNb).GetRootNode();

        if (ExpressionObjectFinder::CheckIfHasObject(platform, globalObjectsContainer, objectsContainer, "string", *node, name)) {
          deleteMe = true;
          break;
        }
      }
    }

    if (deleteMe) {
      somethingModified = true;
      conditions.Remove(cId);
      cId--;
    } else if (!conditions[cId].GetSubInstructions().empty())
      somethingModified =
          RemoveObjectInConditions(platform,
                                   globalObjectsContainer,
                                   objectsContainer,
                                   conditions[cId].GetSubInstructions(),
                                   name) ||
          somethingModified;
  }

  return somethingModified;
}

void EventsRefactorer::RemoveObjectInEvents(const gd::Platform& platform,
                                            gd::ObjectsContainer& globalObjectsContainer,
                                            gd::ObjectsContainer& objectsContainer,
                                            gd::EventsList& events,
                                            gd::String name) {
  for (std::size_t i = 0; i < events.size(); ++i) {
    vector<gd::InstructionsList*> conditionsVectors =
        events[i].GetAllConditionsVectors();
    for (std::size_t j = 0; j < conditionsVectors.size(); ++j) {
      bool conditionsModified = RemoveObjectInConditions(
          platform, globalObjectsContainer, objectsContainer, *conditionsVectors[j], name);
    }

    vector<gd::InstructionsList*> actionsVectors =
        events[i].GetAllActionsVectors();
    for (std::size_t j = 0; j < actionsVectors.size(); ++j) {
      bool actionsModified = RemoveObjectInActions(
          platform, globalObjectsContainer, objectsContainer, *actionsVectors[j], name);
    }

    if (events[i].CanHaveSubEvents())
      RemoveObjectInEvents(
          platform, globalObjectsContainer, objectsContainer, events[i].GetSubEvents(), name);
  }
}

std::vector<EventsSearchResult> EventsRefactorer::ReplaceStringInEvents(
    gd::ObjectsContainer& project,
    gd::ObjectsContainer& layout,
    gd::EventsList& events,
    gd::String toReplace,
    gd::String newString,
    bool matchCase,
    bool inConditions,
    bool inActions,
    bool inEventStrings) {
  vector<EventsSearchResult> modifiedEvents;
  if (toReplace.empty()) return modifiedEvents;

  for (std::size_t i = 0; i < events.size(); ++i) {
    bool eventModified = false;
    if (inConditions) {
      vector<gd::InstructionsList*> conditionsVectors =
          events[i].GetAllConditionsVectors();
      for (std::size_t j = 0; j < conditionsVectors.size(); ++j) {
        bool conditionsModified =
            ReplaceStringInConditions(project,
                                      layout,
                                      *conditionsVectors[j],
                                      toReplace,
                                      newString,
                                      matchCase);
        if (conditionsModified && !eventModified) {
          modifiedEvents.push_back(EventsSearchResult(
              std::weak_ptr<gd::BaseEvent>(events.GetEventSmartPtr(i)),
              &events,
              i));
          eventModified = true;
        }
      }
    }

    if (inActions) {
      vector<gd::InstructionsList*> actionsVectors =
          events[i].GetAllActionsVectors();
      for (std::size_t j = 0; j < actionsVectors.size(); ++j) {
        bool actionsModified = ReplaceStringInActions(project,
                                                      layout,
                                                      *actionsVectors[j],
                                                      toReplace,
                                                      newString,
                                                      matchCase);
        if (actionsModified && !eventModified) {
          modifiedEvents.push_back(EventsSearchResult(
              std::weak_ptr<gd::BaseEvent>(events.GetEventSmartPtr(i)),
              &events,
              i));
          eventModified = true;
        }
      }
    }

    if (inEventStrings) {
      bool eventStringModified = ReplaceStringInEventSearchableStrings(
          project, layout, events[i], toReplace, newString, matchCase);
      if (eventStringModified && !eventModified) {
        modifiedEvents.push_back(EventsSearchResult(
            std::weak_ptr<gd::BaseEvent>(events.GetEventSmartPtr(i)),
            &events,
            i));
        eventModified = true;
      }
    }

    if (events[i].CanHaveSubEvents()) {
      std::vector<EventsSearchResult> modifiedSubEvent =
          ReplaceStringInEvents(project,
                                layout,
                                events[i].GetSubEvents(),
                                toReplace,
                                newString,
                                matchCase,
                                inConditions,
                                inActions,
                                inEventStrings);
      std::copy(modifiedSubEvent.begin(),
                modifiedSubEvent.end(),
                std::back_inserter(modifiedEvents));
    }
  }
  return modifiedEvents;
}

gd::String ReplaceAllOccurrencesCaseInsensitive(gd::String context,
                                               gd::String from,
                                               const gd::String& to) {
  size_t lookHere = 0;
  size_t foundHere;
  size_t fromSize = from.size();
  size_t toSize = to.size();
  while ((foundHere = context.FindCaseInsensitive(from, lookHere)) !=
         gd::String::npos) {
    context.replace(foundHere, fromSize, to);
    lookHere = foundHere + toSize;
  }

  return context;
}

bool EventsRefactorer::ReplaceStringInActions(gd::ObjectsContainer& project,
                                              gd::ObjectsContainer& layout,
                                              gd::InstructionsList& actions,
                                              gd::String toReplace,
                                              gd::String newString,
                                              bool matchCase) {
  bool somethingModified = false;

  for (std::size_t aId = 0; aId < actions.size(); ++aId) {
    for (std::size_t pNb = 0; pNb < actions[aId].GetParameters().size();
         ++pNb) {
      gd::String newParameter =
          matchCase
              ? actions[aId].GetParameter(pNb).GetPlainString().FindAndReplace(
                    toReplace, newString, true)
              : ReplaceAllOccurrencesCaseInsensitive(
                    actions[aId].GetParameter(pNb).GetPlainString(),
                    toReplace,
                    newString);

      if (newParameter != actions[aId].GetParameter(pNb).GetPlainString()) {
        actions[aId].SetParameter(pNb, gd::Expression(newParameter));
        somethingModified = true;
      }
    }

    if (!actions[aId].GetSubInstructions().empty())
      ReplaceStringInActions(project,
                             layout,
                             actions[aId].GetSubInstructions(),
                             toReplace,
                             newString,
                             matchCase);
  }

  return somethingModified;
}

bool EventsRefactorer::ReplaceStringInConditions(
    gd::ObjectsContainer& project,
    gd::ObjectsContainer& layout,
    gd::InstructionsList& conditions,
    gd::String toReplace,
    gd::String newString,
    bool matchCase) {
  bool somethingModified = false;

  for (std::size_t cId = 0; cId < conditions.size(); ++cId) {
    for (std::size_t pNb = 0; pNb < conditions[cId].GetParameters().size();
         ++pNb) {
      gd::String newParameter =
          matchCase ? conditions[cId]
                          .GetParameter(pNb)
                          .GetPlainString()
                          .FindAndReplace(toReplace, newString, true)
                    : ReplaceAllOccurrencesCaseInsensitive(
                          conditions[cId].GetParameter(pNb).GetPlainString(),
                          toReplace,
                          newString);

      if (newParameter != conditions[cId].GetParameter(pNb).GetPlainString()) {
        conditions[cId].SetParameter(pNb, gd::Expression(newParameter));
        somethingModified = true;
      }
    }

    if (!conditions[cId].GetSubInstructions().empty())
      ReplaceStringInConditions(project,
                                layout,
                                conditions[cId].GetSubInstructions(),
                                toReplace,
                                newString,
                                matchCase);
  }

  return somethingModified;
}

bool EventsRefactorer::ReplaceStringInEventSearchableStrings(
    gd::ObjectsContainer& project,
    gd::ObjectsContainer& layout,
    gd::BaseEvent& event,
    gd::String toReplace,
    gd::String newString,
    bool matchCase) {
  vector<gd::String> newEventStrings;
  vector<gd::String> stringEvent = event.GetAllSearchableStrings();

  for (std::size_t sNb = 0; sNb < stringEvent.size(); ++sNb) {
    gd::String newStringEvent =
        matchCase ? stringEvent[sNb].FindAndReplace(toReplace, newString, true)
                  : ReplaceAllOccurrencesCaseInsensitive(
                        stringEvent[sNb], toReplace, newString);
    newEventStrings.push_back(newStringEvent);
  }

  bool somethingModified = event.ReplaceAllSearchableStrings(newEventStrings);

  return somethingModified;
}

vector<EventsSearchResult> EventsRefactorer::SearchInEvents(
    const gd::Platform& platform,
    gd::EventsList& events,
    gd::String search,
    bool matchCase,
    bool inConditions,
    bool inActions,
    bool inEventStrings,
    bool inEventSentences) {
  vector<EventsSearchResult> results;

  const gd::String& ignored_characters =
      EventsRefactorer::searchIgnoredCharacters;

  if (inEventSentences) {
    // Remove ignored characters only when searching in event sentences.
    search.replace_if(
        search.begin(),
        search.end(),
        [ignored_characters](const char& c) {
          return ignored_characters.find(c) != gd::String::npos;
        },
        "");
    search = search.LeftTrim().RightTrim();
    search.RemoveConsecutiveOccurrences(search.begin(), search.end(), ' ');
  }

  for (std::size_t i = 0; i < events.size(); ++i) {
    bool eventAddedInResults = false;

    if (inConditions) {
      vector<gd::InstructionsList*> conditionsVectors =
          events[i].GetAllConditionsVectors();
      for (std::size_t j = 0; j < conditionsVectors.size(); ++j) {
        if (!eventAddedInResults &&
            SearchStringInConditions(platform,
                                     *conditionsVectors[j],
                                     search,
                                     matchCase,
                                     inEventSentences)) {
          results.push_back(EventsSearchResult(
              std::weak_ptr<gd::BaseEvent>(events.GetEventSmartPtr(i)),
              &events,
              i));
        }
      }
    }

    if (inActions) {
      vector<gd::InstructionsList*> actionsVectors =
          events[i].GetAllActionsVectors();
      for (std::size_t j = 0; j < actionsVectors.size(); ++j) {
        if (!eventAddedInResults && SearchStringInActions(platform,
                                                          *actionsVectors[j],
                                                          search,
                                                          matchCase,
                                                          inEventSentences)) {
          results.push_back(EventsSearchResult(
              std::weak_ptr<gd::BaseEvent>(events.GetEventSmartPtr(i)),
              &events,
              i));
        }
      }
    }

    if (inEventStrings) {
      if (!eventAddedInResults &&
          SearchStringInEvent(events[i], search, matchCase)) {
        results.push_back(EventsSearchResult(
            std::weak_ptr<gd::BaseEvent>(events.GetEventSmartPtr(i)),
            &events,
            i));
      }
    }

    if (events[i].CanHaveSubEvents()) {
      vector<EventsSearchResult> subResults =
          SearchInEvents(platform,
                         events[i].GetSubEvents(),
                         search,
                         matchCase,
                         inConditions,
                         inActions,
                         inEventStrings,
                         inEventSentences);
      std::copy(
          subResults.begin(), subResults.end(), std::back_inserter(results));
    }
  }

  return results;
}

bool EventsRefactorer::SearchStringInActions(const gd::Platform& platform,
                                             gd::InstructionsList& actions,
                                             gd::String search,
                                             bool matchCase,
                                             bool inSentences) {
  for (std::size_t aId = 0; aId < actions.size(); ++aId) {
    for (std::size_t pNb = 0; pNb < actions[aId].GetParameters().size();
         ++pNb) {
      size_t foundPosition =
          matchCase
              ? actions[aId].GetParameter(pNb).GetPlainString().find(search)
              : actions[aId]
                    .GetParameter(pNb)
                    .GetPlainString()
                    .FindCaseInsensitive(search);

      if (foundPosition != gd::String::npos) return true;
    }

    if (inSentences && SearchStringInFormattedText(
                           platform, actions[aId], search, matchCase, false))
      return true;

    if (!actions[aId].GetSubInstructions().empty() &&
        SearchStringInActions(platform,
                              actions[aId].GetSubInstructions(),
                              search,
                              matchCase,
                              inSentences))
      return true;
  }

  return false;
}

bool EventsRefactorer::SearchStringInFormattedText(const gd::Platform& platform,
                                                   gd::Instruction& instruction,
                                                   gd::String search,
                                                   bool matchCase,
                                                   bool isCondition) {
  const auto& metadata = isCondition
                             ? gd::MetadataProvider::GetConditionMetadata(
                                   platform, instruction.GetType())
                             : gd::MetadataProvider::GetActionMetadata(
                                   platform, instruction.GetType());
  gd::String completeSentence =
      gd::InstructionSentenceFormatter::Get()->GetFullText(instruction,
                                                           metadata);

  const gd::String& ignored_characters =
      EventsRefactorer::searchIgnoredCharacters;

  completeSentence.replace_if(
      completeSentence.begin(),
      completeSentence.end(),
      [ignored_characters](const char& c) {
        return ignored_characters.find(c) != gd::String::npos;
      },
      "");

  completeSentence.RemoveConsecutiveOccurrences(
      completeSentence.begin(), completeSentence.end(), ' ');

  size_t foundPosition = matchCase
                             ? completeSentence.find(search)
                             : completeSentence.FindCaseInsensitive(search);

  return foundPosition != gd::String::npos;
}

bool EventsRefactorer::SearchStringInConditions(
    const gd::Platform& platform,
    gd::InstructionsList& conditions,
    gd::String search,
    bool matchCase,
    bool inSentences) {
  for (std::size_t cId = 0; cId < conditions.size(); ++cId) {
    for (std::size_t pNb = 0; pNb < conditions[cId].GetParameters().size();
         ++pNb) {
      size_t foundPosition =
          matchCase
              ? conditions[cId].GetParameter(pNb).GetPlainString().find(search)
              : conditions[cId]
                    .GetParameter(pNb)
                    .GetPlainString()
                    .FindCaseInsensitive(search);

      if (foundPosition != gd::String::npos) return true;
    }

    if (inSentences && SearchStringInFormattedText(
                           platform, conditions[cId], search, matchCase, true))
      return true;

    if (!conditions[cId].GetSubInstructions().empty() &&
        SearchStringInConditions(platform,
                                 conditions[cId].GetSubInstructions(),
                                 search,
                                 matchCase,
                                 inSentences))
      return true;
  }

  return false;
}

bool EventsRefactorer::SearchStringInEvent(gd::BaseEvent& event,
                                           gd::String search,
                                           bool matchCase) {
  for (gd::String str : event.GetAllSearchableStrings()) {
    if (matchCase) {
      if (str.find(search) != gd::String::npos) return true;
    } else {
      if (str.FindCaseInsensitive(search) != gd::String::npos) return true;
    }
  }

  return false;
}

EventsSearchResult::EventsSearchResult(std::weak_ptr<gd::BaseEvent> event_,
                                       gd::EventsList* eventsList_,
                                       std::size_t positionInList_)
    : event(event_), eventsList(eventsList_), positionInList(positionInList_) {}

EventsSearchResult::EventsSearchResult()
    : eventsList(NULL), positionInList(0) {}

}  // namespace gd
