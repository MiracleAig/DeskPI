import QtQuick

Canvas {
    id: icon
    property color ink: "#f5f5f5"
    width: 28
    height: 28
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.strokeStyle = ink
        ctx.lineWidth = 2
        ctx.beginPath()
        ctx.arc(width / 2, height / 2, 5, 0, Math.PI * 2)
        ctx.stroke()
        for (let i = 0; i < 8; ++i) {
            const a = i * Math.PI / 4
            ctx.beginPath()
            ctx.moveTo(width / 2 + Math.cos(a) * 9, height / 2 + Math.sin(a) * 9)
            ctx.lineTo(width / 2 + Math.cos(a) * 12, height / 2 + Math.sin(a) * 12)
            ctx.stroke()
        }
    }
}
