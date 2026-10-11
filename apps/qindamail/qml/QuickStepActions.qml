// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App
Tk.Flex {
    id: editor
    property MailService service: null
    property string mailboxId: "local"
    property var actions: []
    readonly property var kinds: [
        {id:"markRead",label:qsTr("Mark read / unread")}, {id:"flag",label:qsTr("Follow-up flag")},
        {id:"categories",label:qsTr("Categories")}, {id:"copy",label:qsTr("Copy to folder")},
        {id:"replyDraft",label:qsTr("Create reply draft")}, {id:"forwardDraft",label:qsTr("Create forward draft")},
        {id:"move",label:qsTr("Move to folder (last)")}]
    signal edited(var actions)
    direction: Tk.Flex.Column; gap: Tk.Theme.space.sm
    function edit(index,key,value) {
        const next=Array.from(actions); next[index]=Object.assign({},next[index]); next[index][key]=value; edited(next)
    }
    function move(index,delta) {
        const next=Array.from(actions), target=index+delta
        if (target<0 || target>=next.length) return
        const value=next.splice(index,1)[0]; next.splice(target,0,value); edited(next)
    }
    function remove(index) { const next=Array.from(actions); next.splice(index,1); edited(next) }
    function add(kind) {
        const action={kind:kind}
        if (kind==="markRead") action.read=true
        if (kind==="flag") action.preset="today"
        if (kind==="categories") { action.mode="add"; action.names=[] }
        edited(Array.from(actions).concat([action]))
    }
    Repeater {
        model: editor.actions
        delegate: Tk.Box {
            id: actionBox
            required property int index
            required property var modelData
            readonly property string kind: String(modelData.kind)
            objectName: "quickStepAction_"+index
            padding: Tk.Theme.space.sm; color: Tk.Theme.color.panel; borderColor: Tk.Theme.color.border
            Tk.Flex {
                direction: Tk.Flex.Column; gap: Tk.Theme.space.xs
                Tk.Flex {
                    direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
                    Tk.Label { text:(actionBox.index+1)+". "+(editor.kinds.find(k=>k.id===actionBox.kind)||{}).label; Tk.Flex.grow:1; Tk.Flex.basis:0 }
                    Tk.IconButton { objectName:"quickStepActionUp_"+actionBox.index; iconName:"arrow-up"; tooltip:qsTr("Run this action earlier"); enabled:actionBox.index>0; onClicked:editor.move(actionBox.index,-1) }
                    Tk.IconButton { objectName:"quickStepActionDown_"+actionBox.index; iconName:"arrow-down"; tooltip:qsTr("Run this action later"); enabled:actionBox.index+1<editor.actions.length; onClicked:editor.move(actionBox.index,1) }
                    Tk.IconButton { objectName:"quickStepActionRemove_"+actionBox.index; iconName:"x"; tooltip:qsTr("Remove action"); onClicked:editor.remove(actionBox.index) }
                }
                Tk.ComboBox {
                    objectName:"quickStepRead_"+actionBox.index; visible:actionBox.kind==="markRead"
                    model:[qsTr("Mark as read"),qsTr("Mark as unread")]; currentIndex:actionBox.modelData.read===false?1:0
                    onActivated:index=>editor.edit(actionBox.index,"read",index===0)
                }
                Tk.ComboBox {
                    objectName:"quickStepFlag_"+actionBox.index; visible:actionBox.kind==="flag"
                    readonly property var presets:["today","tomorrow","thisWeek","nextWeek","noDate","complete","clear"]
                    model:[qsTr("Today"),qsTr("Tomorrow"),qsTr("This week"),qsTr("Next week"),qsTr("No date"),qsTr("Complete"),qsTr("Clear")]
                    currentIndex:Math.max(0,presets.indexOf(actionBox.modelData.preset))
                    onActivated:index=>editor.edit(actionBox.index,"preset",presets[index])
                }
                Tk.ComboBox {
                    objectName:"quickStepFolder_"+actionBox.index; visible:actionBox.kind==="move"||actionBox.kind==="copy"
                    model:[{key:"",name:qsTr("Choose a destination…")}].concat(editor.service?Array.from(editor.service.folderTargets(editor.mailboxId)):[])
                    textRole:"name"; currentIndex:Math.max(0,model.findIndex(f=>f.key===actionBox.modelData.folderKey))
                    onActivated:index=>editor.edit(actionBox.index,"folderKey",model[index].key)
                }
                Tk.Flex {
                    visible:actionBox.kind==="categories"; direction:Tk.Flex.Column; gap:Tk.Theme.space.xs
                    Tk.ComboBox {
                        objectName:"quickStepCategoryMode_"+actionBox.index
                        readonly property var modes:["add","replace","clear"]
                        model:[qsTr("Add categories"),qsTr("Replace categories"),qsTr("Clear categories")]
                        currentIndex:Math.max(0,modes.indexOf(actionBox.modelData.mode||"add"))
                        onActivated:index=>editor.edit(actionBox.index,"mode",modes[index])
                    }
                    Tk.TextField {
                        objectName:"quickStepCategoryNames_"+actionBox.index; visible:actionBox.modelData.mode!=="clear"
                        text:(actionBox.modelData.names||[]).join(", "); placeholderText:qsTr("Category names, separated by commas")
                        onTextEdited:editor.edit(actionBox.index,"names",text.split(",").map(s=>s.trim()).filter(s=>s.length))
                    }
                }
                Tk.Flex {
                    visible:actionBox.kind==="replyDraft"||actionBox.kind==="forwardDraft"
                    direction:Tk.Flex.Column; gap:Tk.Theme.space.xs
                    Tk.CheckBox { objectName:"quickStepReplyAll_"+actionBox.index; visible:actionBox.kind==="replyDraft"; text:qsTr("Reply to all"); checked:actionBox.modelData.replyAll===true; onToggled:editor.edit(actionBox.index,"replyAll",checked) }
                    Tk.TextField { objectName:"quickStepRecipients_"+actionBox.index; text:actionBox.modelData.recipients||""; placeholderText:qsTr("Recipients (optional)"); onTextEdited:editor.edit(actionBox.index,"recipients",text) }
                    Tk.TextArea { objectName:"quickStepDraftBody_"+actionBox.index; text:actionBox.modelData.body||""; placeholderText:qsTr("Text to add before the quoted message"); rows:3; onTextChanged:if(activeFocus)editor.edit(actionBox.index,"body",text) }
                    Tk.Caption { text:qsTr("Saved as a draft for review. Nothing is sent."); wrapMode:Text.Wrap }
                }
            }
        }
    }
    Tk.Flex {
        direction:Tk.Flex.Row; gap:Tk.Theme.space.sm; wrap:Tk.Flex.Wrap
        Tk.ComboBox { id:addKind; objectName:"quickStepAddKind"; model:editor.kinds; textRole:"label"; Tk.Flex.grow:1; Tk.Flex.basis:0 }
        Tk.Button { objectName:"quickStepAddAction"; text:qsTr("Add action"); iconName:"plus"; enabled:editor.actions.length<16; onClicked:editor.add(editor.kinds[addKind.currentIndex].id) }
    }
}
