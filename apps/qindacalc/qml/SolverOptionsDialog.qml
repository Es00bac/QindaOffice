// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Solver's Options (M90, D-098): Excel's three tabs. All Methods —
// Constraint Precision, Use Automatic Scaling, Show Iteration Results,
// Ignore Integer Constraints, Integer Optimality (%), Max Time, Iterations,
// Max Subproblems, Max Feasible Solutions (empty: unlimited). GRG Nonlinear
// — Convergence, Derivatives Forward / Central, Multistart with Population
// Size, Random Seed and Require Bounds on Variables. Evolutionary —
// Convergence, Mutation Rate, Population Size, Random Seed, Maximum Time
// without improvement, Require Bounds on Variables. OK hands the options
// back to Solver Parameters; nothing is stored until Solve or Close.
Tk.Dialog {
    id: dialog

    property var options: ({})

    signal saved(var options)

    // Excel's labels are long ("Maximum Time without improvement").
    readonly property real labelWidth: Tk.Theme.size.labelWidth * 1.9

    objectName: "solverOptionsDialog"
    title: qsTr("Options")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.15
    primaryText: qsTr("OK")

    function load(method, options) {
        dialog.options = options
        tabs.currentIndex = method === 1 ? 1 : (method === 3 ? 2 : 0)
        precision.text = options.precision || "0.000001"
        autoScale.checked = options.autoScale !== false
        showIterations.checked = options.showIterations === true
        ignoreIntegers.checked = options.ignoreIntegers === true
        integerOptimality.text = options.integerOptimality || "1"
        maxTime.text = options.maxTime || ""
        iterations.text = options.iterations || ""
        maxSubproblems.text = options.maxSubproblems || ""
        maxFeasible.text = options.maxFeasible || ""
        convergence.text = options.convergence || "0.0001"
        derivatives.currentIndex = options.centralDerivatives === true ? 1 : 0
        multistart.checked = options.multistart === true
        population.text = options.populationSize || "100"
        seed.text = options.randomSeed || "0"
        requireBounds.checked = options.requireBounds !== false
        mutation.text = options.mutationRate || "0.075"
        noImprovement.text = options.maxTimeWithoutImprovement || "30"
    }
    onAccepted: {
        const out = Object.assign({}, dialog.options)
        Object.assign(out, {
            precision: precision.text, autoScale: autoScale.checked, showIterations: showIterations.checked,
            ignoreIntegers: ignoreIntegers.checked, integerOptimality: integerOptimality.text, maxTime: maxTime.text,
            iterations: iterations.text, maxSubproblems: maxSubproblems.text, maxFeasible: maxFeasible.text,
            convergence: tabs.currentIndex === 2 ? evolutionConvergence.text : convergence.text,
            centralDerivatives: derivatives.currentIndex === 1, multistart: multistart.checked,
            populationSize: tabs.currentIndex === 2 ? evolutionPopulation.text : population.text,
            randomSeed: tabs.currentIndex === 2 ? evolutionSeed.text : seed.text,
            requireBounds: tabs.currentIndex === 2 ? evolutionBounds.checked : requireBounds.checked,
            mutationRate: mutation.text, maxTimeWithoutImprovement: noImprovement.text
        })
        dialog.saved(out)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.TabStrip {
            id: tabs
            objectName: "solverOptionsTabs"
            model: [qsTr("All Methods"), qsTr("GRG Nonlinear"), qsTr("Evolutionary")]
        }
        // ---- All Methods ----
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            visible: tabs.currentIndex === 0
            Tk.PropertyRow { labelWidth: dialog.labelWidth; label: qsTr("Constraint Precision"); Tk.TextField { id: precision; objectName: "solverPrecision"; mono: true } }
            Tk.CheckBox { id: autoScale; objectName: "solverAutoScale"; text: qsTr("Use Automatic Scaling") }
            Tk.CheckBox { id: showIterations; objectName: "solverShowIterations"; text: qsTr("Show Iteration Results") }
            Tk.SectionHeader { title: qsTr("Solving with Integer Constraints") }
            Tk.CheckBox { id: ignoreIntegers; objectName: "solverIgnoreIntegers"; text: qsTr("Ignore Integer Constraints") }
            Tk.PropertyRow {
                labelWidth: dialog.labelWidth
                label: qsTr("Integer Optimality (%)")
                Tk.TextField { id: integerOptimality; objectName: "solverIntegerOptimality"; mono: true }
            }
            Tk.SectionHeader { title: qsTr("Solving Limits") }
            Tk.PropertyRow { labelWidth: dialog.labelWidth; label: qsTr("Max Time (Seconds)"); Tk.TextField { id: maxTime; objectName: "solverMaxTime"; mono: true } }
            Tk.PropertyRow { labelWidth: dialog.labelWidth; label: qsTr("Iterations"); Tk.TextField { id: iterations; objectName: "solverIterations"; mono: true } }
            Tk.SectionHeader { title: qsTr("Evolutionary and Integer Constraints") }
            Tk.PropertyRow {
                labelWidth: dialog.labelWidth
                label: qsTr("Max Subproblems")
                Tk.TextField { id: maxSubproblems; objectName: "solverMaxSubproblems"; mono: true }
            }
            Tk.PropertyRow {
                labelWidth: dialog.labelWidth
                label: qsTr("Max Feasible Solutions")
                Tk.TextField { id: maxFeasible; objectName: "solverMaxFeasible"; mono: true }
            }
        }
        // ---- GRG Nonlinear ----
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            visible: tabs.currentIndex === 1
            Tk.PropertyRow { labelWidth: dialog.labelWidth; label: qsTr("Convergence"); Tk.TextField { id: convergence; objectName: "solverConvergence"; mono: true } }
            Tk.PropertyRow {
                labelWidth: dialog.labelWidth
                label: qsTr("Derivatives")
                Tk.Segmented { id: derivatives; objectName: "solverDerivatives"; small: true; model: [qsTr("Forward"), qsTr("Central")] }
            }
            Tk.SectionHeader { title: qsTr("Multistart") }
            Tk.CheckBox { id: multistart; objectName: "solverMultistart"; text: qsTr("Use Multistart") }
            Tk.PropertyRow { labelWidth: dialog.labelWidth; label: qsTr("Population Size"); Tk.TextField { id: population; objectName: "solverPopulation"; mono: true } }
            Tk.PropertyRow { labelWidth: dialog.labelWidth; label: qsTr("Random Seed"); Tk.TextField { id: seed; objectName: "solverSeed"; mono: true } }
            Tk.CheckBox { id: requireBounds; objectName: "solverRequireBounds"; text: qsTr("Require Bounds on Variables") }
        }
        // ---- Evolutionary ----
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            visible: tabs.currentIndex === 2
            Tk.PropertyRow {
                labelWidth: dialog.labelWidth
                label: qsTr("Convergence")
                Tk.TextField { id: evolutionConvergence; objectName: "solverEvolutionConvergence"; mono: true; text: convergence.text }
            }
            Tk.PropertyRow { labelWidth: dialog.labelWidth; label: qsTr("Mutation Rate"); Tk.TextField { id: mutation; objectName: "solverMutation"; mono: true } }
            Tk.PropertyRow {
                labelWidth: dialog.labelWidth
                label: qsTr("Population Size")
                Tk.TextField { id: evolutionPopulation; objectName: "solverEvolutionPopulation"; mono: true; text: population.text }
            }
            Tk.PropertyRow {
                labelWidth: dialog.labelWidth
                label: qsTr("Random Seed")
                Tk.TextField { id: evolutionSeed; objectName: "solverEvolutionSeed"; mono: true; text: seed.text }
            }
            Tk.PropertyRow {
                labelWidth: dialog.labelWidth
                label: qsTr("Maximum Time without improvement")
                Tk.TextField { id: noImprovement; objectName: "solverNoImprovement"; mono: true }
            }
            Tk.CheckBox {
                id: evolutionBounds
                objectName: "solverEvolutionBounds"
                text: qsTr("Require Bounds on Variables")
                checked: requireBounds.checked
            }
        }
    }
}
