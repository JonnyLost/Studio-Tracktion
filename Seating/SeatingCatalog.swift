// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

enum SeatingCatalog {
    // Stable keys, display names, and optional search aliases. Source links are in
    // docs/orchestral-seating.md. An entry represents a physical seating choice.
    static let instruments: [SeatingInstrument] = [
        entries(1, .strings, """
        violin-1|Solo Violin 1
        violin-2|Solo Violin 2
        viola|Solo Viola
        cello-1|Solo Cello 1
        cello-2|Solo Cello 2
        bass|Solo Double Bass|contrabass
        violin-section|Orchestral Violins (14)|violin ensemble
        viola-section|Orchestral Violas (10)|viola ensemble
        cello-section|Orchestral Cellos (8)|cello ensemble
        bass-section|Orchestral Double Basses (6)|bass ensemble
        harp|Harp I|harp 1
        """),
        entries(1, .winds, """
        piccolo|Piccolo Flute
        flute|Flute 1
        oboe|French Oboe
        english-horn|English Horn (French)|cor anglais
        clarinet|Clarinet in B♭|clarinet in Bb B-flat
        bass-clarinet|Bass Clarinet
        bassoon|Bassoon
        contrabassoon|Contrabassoon|contra bassoon
        """),
        entries(1, .brass, """
        triple-horn|Triple Horn|French horn
        horns|Horn Ensemble (4)|French horns
        trumpet|Trumpet in C|C trumpet
        trumpets|Trumpet Ensemble (3)
        trombone|Tenor Trombone
        bass-trombone|Bass Trombone
        trombones|Trombone Ensemble (3)
        tuba|Bass Tuba
        """),
        entries(1, .percussion, """
        timpani|Timpani|kettledrums
        taiko|Taiko|drums
        bass-drum|Bass Drum|drums
        snare|Snare Drum|drums
        snare-ensemble|Snare Drums a4|drums
        tambourine|Tambourine|drums
        tom|Concert Tom|drums
        gong|Gong|cymbals and gongs
        suspended-cymbal|Suspended Cymbal
        piatti|Piatti|crash cymbals
        tam-tam|Tam-tam|gong
        thunder|Thundersheet
        rails|Rails
        chimes|Wind Chimes
        castanets|Castanets
        triangle|Triangle
        crotales|Crotales
        tubular-bells|Tubular Bells
        plate-bells|Plate Bells
        glockenspiel|Glockenspiel
        xylophone|Xylophone
        vibraphone|Vibraphone
        marimbaphone|Marimbaphone|marimba
        """),
        entries(1, .keys, """
        bosendorfer|Bösendorfer Grand Piano
        d274|Synchron D-274 Grand Piano|concert d steinway
        celesta|Celesta
        """),
        entries(2, .strings, """
        violin-section|Chamber Violins (6)
        viola-section|Chamber Violas (4)
        cello-section|Chamber Cellos (3)
        bass-section|Chamber Double Basses (2)
        guitar|Concert Guitar
        electric-guitar|Overdrive Electric Guitar|distorted
        upright-bass|Upright Bass
        appassionata|Appassionata Strings (Bonus)
        """),
        entries(2, .winds, """
        flute|Flute 2
        alto-flute|Alto Flute
        flutes|Flute Ensemble (3)
        oboe|Viennese Oboe
        oboe-amore|Oboe d'Amore
        oboes|Oboe Ensemble (3)
        clarinet|Clarinet in E♭|Eb E-flat small clarinet
        basset-horn|Basset Horn
        clarinets|Clarinet Ensemble (3)
        bassoons|Bassoon Ensemble (3)
        alto-sax|Alto Saxophone
        tenor-sax|Tenor Saxophone
        baritone-sax|Baritone Saxophone
        """),
        entries(2, .brass, """
        horn|Viennese Horn
        horns|Horn Ensemble (8)
        piccolo-trumpet|Piccolo Trumpet
        muted-trumpet|Trumpet in C (muted)
        muted-trumpets|Trumpet Ensemble (3, muted)
        muted-trombone|Tenor Trombone (muted)
        contrabass-trombone|Contrabass Trombone
        muted-trombones|Trombone Ensemble (3, muted)
        tuba|Contrabass Tuba
        wagner-tuba|Wagner Tuba
        """),
        entries(2, .keys, """
        harpsichord|Harpsichord|cembalo
        organ|Konzerthaus Organ|pipe organ
        harmonium|Harmonium
        prepared-piano|Prepared Piano
        """),
        entries(2, .percussion, """
        timpani|Synchron Timpani
        bass-drum|Synchron Bass Drum
        snare|Synchron Snare Drum
        tambourine|Synchron Tambourine
        toms|Synchron Concert Toms
        piatti|Synchron Piatti
        cymbal|Synchron Cymbal
        tam-tam|Synchron Tam-tam
        """),
        entries(3, .strings, """
        violin|Solo Violin (con sordino)|muted
        viola|Solo Viola (con sordino)|muted
        cello|Solo Cello (con sordino)|muted
        bass|Solo Double Bass (con sordino)|muted
        chamber-violins|Chamber Violins (con sordino)|muted
        chamber-violas|Chamber Violas (con sordino)|muted
        chamber-cellos|Chamber Cellos (con sordino)|muted
        chamber-basses|Chamber Double Basses (con sordino)|muted
        appassionata-violins|Appassionata Violins (20)
        appassionata-violas|Appassionata Violas (14)
        appassionata-cellos|Appassionata Cellos (12)
        appassionata-basses|Appassionata Double Basses (10)
        muted-appassionata-violins|Appassionata Violins (con sordino)|muted
        muted-appassionata-violas|Appassionata Violas (con sordino)|muted
        muted-appassionata-cellos|Appassionata Cellos (con sordino)|muted
        muted-appassionata-basses|Appassionata Double Basses (con sordino)|muted
        harp|Harp II|harp 2
        """),
        entries(4, .winds, """
        flute|Bass Flute
        english-horn|English Horn (Viennese)|cor anglais
        heckelphone|Heckelphone
        clarinet|Contrabass Clarinet
        bassoon|Bassoon 2
        soprano-sax|Soprano Saxophone
        bass-sax|Bass Saxophone
        """),
        entries(4, .brass, """
        trumpets|Fanfare Trumpets (6)
        bass-trumpet|Bass Trumpet
        cornet|Cornet
        flugelhorn|Flugelhorn
        trumpet|Trumpet in B♭ II (muted)|Bb B-flat
        alto-trombone|Alto Trombone
        euphonium|Euphonium
        cimbasso|Cimbasso
        """),
        entries(4, .voices, """
        soprano-choir|Vienna Choir – Soprano (16)
        alto-choir|Vienna Choir – Alto (10)
        tenor-choir|Vienna Choir – Tenor (10)
        bass-choir|Vienna Choir – Bass (8)
        soprano-solo|Vienna Solo Voices – Soprano
        alto-solo|Vienna Solo Voices – Alto
        tenor-solo|Vienna Solo Voices – Tenor
        bass-solo|Vienna Solo Voices – Bass
        """),
        entries(5, .strings, """
        violins-i|Dimension First Violins (8)|regular muted con sordino
        violins-ii|Dimension Second Violins (8)|regular muted con sordino
        violas|Dimension Violas (6)|regular muted con sordino
        cellos|Dimension Cellos (6)|regular muted con sordino
        basses|Dimension Double Basses (4)|regular muted con sordino
        """),
        entries(6, .brass, """
        horns|Dimension Horns (4)|regular muted French horns
        trumpets|Dimension Trumpets (4)|regular muted
        trombones|Dimension Trombones (4)|regular muted
        wagner-tubas|Dimension Wagner Tubas (4)
        low-brass|Dimension Low Brass (4)|tenor trombones bass trombone tuba
        """),
        entries(7, .historic, """
        transverse-flute|Transverse Flute|traverso
        oboe|Baroque Oboe
        oboe-caccia|Oboe da Caccia
        cornett|Cornett|zink
        crumhorn|Crumhorn|krumhorn
        soprano-recorder|Soprano Recorder
        alto-recorder|Alto Recorder
        tenor-recorder|Tenor Recorder
        bass-recorder|Bass Recorder
        ophicleide|Ophicleide
        serpent|Serpent
        trumpet-bb|Natural Trumpet in B♭|Bb B-flat
        trumpet-c|Natural Trumpet in C
        trumpet-d|Natural Trumpet in D
        horn-bb-alto|Natural Horn in B♭ (alto)|Bb B-flat
        horn-bb-basso|Natural Horn in B♭ (basso)|Bb B-flat
        horn-c-alto|Natural Horn in C (alto)
        horn-c-basso|Natural Horn in C (basso)
        horn-eb|Natural Horn in E♭|Eb E-flat
        horn-f|Natural Horn in F
        horn-g|Natural Horn in G
        glass-harmonica|Glass Harmonica
        """)
    ].flatMap { $0 }

    static let byID = Dictionary(uniqueKeysWithValues: instruments.map { ($0.id, $0) })

    private static func entries(_ volume: Int, _ family: SeatingFamily, _ rows: String) -> [SeatingInstrument] {
        rows.split(separator: "\n").map { row in
            let fields = row.split(separator: "|", maxSplits: 2)
            return SeatingInstrument(id: "v\(volume)-\(fields[0])", name: String(fields[1]), family: family,
                                     volume: volume, aliases: fields.count == 3 ? String(fields[2]) : "")
        }
    }

    static func modernSymphony() -> SeatingLayout {
        var layout = SeatingLayout(name: "Modern Symphony")
        // Conductor's view, with first and second violins together on the left.
        let seats: [(String, String, String, Double, Double)] = [
            ("violin-section", "Violin I", "First Violins", -0.68, 0.15),
            ("violin-section", "Violin II", "Second Violins", -0.38, 0.18),
            ("viola-section", "Viola", "Violas", -0.08, 0.17),
            ("cello-section", "Cello", "Cellos", 0.24, 0.16),
            ("bass-section", "Bass", "Double Basses", 0.67, 0.20),
            ("harp", "Harp", "Harp", -0.69, 0.44),
            ("d274", "Piano", "Keyboards", -0.55, 0.63),
            ("piccolo", "Piccolo", "Flutes", -0.37, 0.44),
            ("flute", "Flute", "Flutes", -0.20, 0.38),
            ("oboe", "Oboe", "Oboes", 0.00, 0.38),
            ("english-horn", "Eng. Horn", "Oboes", 0.18, 0.38),
            ("clarinet", "Clarinet", "Clarinets", 0.37, 0.38),
            ("bass-clarinet", "Bass Cl.", "Clarinets", 0.57, 0.42),
            ("bassoon", "Bassoon", "Bassoons", 0.72, 0.49),
            ("contrabassoon", "C. Bsn.", "Bassoons", 0.53, 0.58),
            ("horns", "Horns", "Horns", -0.34, 0.63),
            ("trumpets", "Trumpets", "Trumpets", -0.13, 0.62),
            ("trombone", "Tbn.", "Trombones", 0.06, 0.61),
            ("bass-trombone", "Bass Tbn.", "Trombones", 0.25, 0.60),
            ("tuba", "Tuba", "Tuba", 0.40, 0.72),
            ("timpani", "Timpani", "Percussion", -0.30, 0.82),
            ("bass-drum", "Bass Drum", "Percussion", -0.09, 0.84),
            ("piatti", "Cymbals", "Percussion", 0.12, 0.84),
            ("xylophone", "Xylophone", "Percussion", 0.31, 0.82)
        ]
        for (key, label, section, x, depth) in seats {
            if let instrument = byID["v1-\(key)"] {
                layout.place(instrument, at: SeatingPoint(x: x, depth: depth), label: label, section: section)
            }
        }
        return layout
    }
}
