// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App
Tk.Dialog {
    id: dialog
    property MailService service: null
    readonly property MailQuickSteps manager: service?service.quickSteps:null
    readonly property bool busy:manager?manager.busy:false
    readonly property bool stopping:manager?manager.cancelRequested:false
    readonly property var outcome:manager?manager.outcome:({})
    readonly property string savedError:manager?manager.lastError:""
    property string mailboxId:"local"
    property var messageIds:[]
    property var editing:({})
    property string errorText:""
    property string requestId:""
    property bool editMode:true
    readonly property var mailboxes:{ if(!manager)return []; void service.accounts; return manager.mailboxes() }
    readonly property var savedSteps:{ if(!manager)return []; void manager.revision; return manager.forMailbox(mailboxId) }
    signal draftRequested(var draft)
    objectName:"quickStepsDialog"
    title:qsTr("Quick Steps")
    dialogWidth:Tk.Theme.size.dialogWidth*1.45
    primaryText:qsTr("Close"); secondaryText:""
    function loadIndex(index) { editing= index>=0&&index<savedSteps.length ? Object.assign({},savedSteps[index]) : manager.newStep(mailboxId); errorText="" }
    function openFor(account,ids,runOnly) {
        mailboxId=mailboxes.some(m=>m.id===account)?account:"local"
        messageIds=Array.from(ids||[]); editMode=!runOnly; requestId=""
        stepBox.currentIndex=savedSteps.length?0:-1; loadIndex(stepBox.currentIndex); open()
    }
    function update(key,value) { const next=Object.assign({},editing); next[key]=value; editing=next }
    function save() {
        const result=manager.saveStep(editing)
        errorText=result.ok?"":result.error
        if(result.ok)stepBox.currentIndex=savedSteps.findIndex(s=>s.id===editing.id)
        return result.ok
    }
    function run() {
        if(editMode&&!save())return
        requestId=manager.execute(editing.id,messageIds)
    }
    Tk.Flex {
        direction:Tk.Flex.Column; gap:Tk.Theme.space.sm
        Tk.Caption { text:qsTr("Reusable actions for one mailbox. Actions run in order; a failure stops the remaining actions."); wrapMode:Text.Wrap }
        Tk.ComboBox {
            id:mailboxBox; objectName:"quickStepMailbox"; model:dialog.mailboxes; textRole:"name"
            currentIndex:Math.max(0,model.findIndex(m=>m.id===dialog.mailboxId)); enabled:!dialog.busy
            onActivated:index=>{ dialog.mailboxId=model[index].id; stepBox.currentIndex=dialog.savedSteps.length?0:-1; dialog.loadIndex(stepBox.currentIndex) }
        }
        Tk.Flex {
            direction:Tk.Flex.Row; gap:Tk.Theme.space.sm; wrap:Tk.Flex.Wrap
            Tk.ComboBox { id:stepBox; objectName:"quickStepSelect"; model:dialog.savedSteps; textRole:"name"; Tk.Flex.grow:1; Tk.Flex.basis:0; enabled:!dialog.busy; onActivated:index=>dialog.loadIndex(index) }
            Tk.Button { objectName:"quickStepNew"; text:qsTr("New"); iconName:"plus"; enabled:!dialog.busy; onClicked:{ dialog.editing=dialog.manager.newStep(dialog.mailboxId); dialog.editMode=true; stepBox.currentIndex=-1 } }
            Tk.Button { objectName:"quickStepManage"; text:dialog.editMode?qsTr("Hide editor"):qsTr("Edit"); onClicked:dialog.editMode=!dialog.editMode }
        }
        Tk.Flex {
            visible:dialog.editMode; enabled:!dialog.busy
            direction:Tk.Flex.Column; gap:Tk.Theme.space.sm
            Tk.TextField { objectName:"quickStepName"; text:dialog.editing.name||""; placeholderText:qsTr("Quick Step name"); onTextEdited:dialog.update("name",text) }
            QuickStepActions { service:dialog.service; mailboxId:dialog.mailboxId; actions:dialog.editing.actions||[]; onEdited:actions=>dialog.update("actions",actions) }
            Tk.Flex {
                direction:Tk.Flex.Row; gap:Tk.Theme.space.sm; wrap:Tk.Flex.Wrap
                Tk.Button { objectName:"quickStepSave"; text:qsTr("Save Quick Step"); iconName:"save"; onClicked:dialog.save() }
                Tk.Button { objectName:"quickStepRemove"; text:qsTr("Delete"); enabled:stepBox.currentIndex>=0; onClicked:{ if(dialog.manager.removeStep(dialog.editing.id)){stepBox.currentIndex=dialog.savedSteps.length?0:-1;dialog.loadIndex(stepBox.currentIndex)}else dialog.errorText=dialog.savedError } }
                Tk.IconButton { objectName:"quickStepEarlier"; iconName:"arrow-up"; tooltip:qsTr("Move Quick Step earlier"); enabled:stepBox.currentIndex>0; onClicked:{ const to=stepBox.currentIndex-1;if(dialog.manager.moveStep(dialog.editing.id,to)){stepBox.currentIndex=to;dialog.loadIndex(to)} } }
                Tk.IconButton { objectName:"quickStepLater"; iconName:"arrow-down"; tooltip:qsTr("Move Quick Step later"); enabled:stepBox.currentIndex>=0&&stepBox.currentIndex+1<dialog.savedSteps.length; onClicked:{ const to=stepBox.currentIndex+1;if(dialog.manager.moveStep(dialog.editing.id,to)){stepBox.currentIndex=to;dialog.loadIndex(to)} } }
            }
        }
        Tk.Notice { visible:dialog.errorText.length>0||dialog.savedError.length>0; variant:"danger"; text:dialog.errorText||dialog.savedError }
        Tk.Flex {
            direction:Tk.Flex.Row; gap:Tk.Theme.space.sm; wrap:Tk.Flex.Wrap
            Tk.Button { objectName:"quickStepRun"; text:qsTr("Run on %1 message(s)").arg(dialog.messageIds.length); iconName:"play"; variant:"accent"; enabled:dialog.messageIds.length>0&&!dialog.busy&&!!dialog.editing.id; onClicked:dialog.run() }
            Tk.Button { objectName:"quickStepCancel"; visible:dialog.busy; text:dialog.stopping?qsTr("Stopping after current action…"):qsTr("Stop"); enabled:!dialog.stopping; onClicked:dialog.manager.cancel() }
        }
        Tk.Notice {
            objectName:"quickStepOutcome"
            visible:!!dialog.outcome.state
            variant:dialog.outcome.state==="accepted"?"success":dialog.outcome.state==="running"?"info":"warning"
            title:qsTr("%1 · %2 of %3 actions completed").arg(dialog.outcome.state||"").arg(dialog.outcome.completed||0).arg(dialog.outcome.total||0)
            text:dialog.outcome.error||""
        }
        Repeater {
            model:(dialog.outcome.results||[]).slice(-20)
            delegate:Tk.Flex {
                id:resultRow
                required property var modelData
                direction:Tk.Flex.Row; gap:Tk.Theme.space.sm
                Tk.Caption { text:resultRow.modelData.action+": "+resultRow.modelData.state+(resultRow.modelData.error?" — "+resultRow.modelData.error:""); wrapMode:Text.Wrap; Tk.Flex.grow:1; Tk.Flex.basis:0 }
                Tk.Button { objectName:"quickStepOpenDraft"; visible:!!resultRow.modelData.draftId; text:qsTr("Open draft"); onClicked:dialog.draftRequested(dialog.manager.draft(resultRow.modelData.draftId)) }
            }
        }
    }
    Connections {
        target:dialog.manager
        function onFinished(id,result) { if(id===dialog.requestId && result.state!=="accepted")dialog.errorText=result.error||result.state }
    }
}
